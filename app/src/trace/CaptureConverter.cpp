// SPDX-License-Identifier: AGPL-3.0-or-later
#include "trace/CaptureConverter.h"

#include <algorithm>
#include <atomic>
#include <complex>
#include <chrono>
#include <cmath>
#include <numbers>

namespace rta::trace {

namespace {

// station-3 T2: the exact inverse of AnalyserPublish.cpp's radians->degrees
// crossing -- same literal, same std::numbers::pi, opposite direction.
[[nodiscard]] float radFromDeg(float deg) noexcept {
    return deg * static_cast<float>(std::numbers::pi / 180.0);
}

// station-3 T3: a function-local static, same shape TraceLibrary.cpp's own
// nextGeneration() uses for the identical reason -- guaranteed initialised
// before first use, no out-of-line definition to forget. A wall-clock
// timestamp cannot serve this role (research A3.4): two calls inside one
// millisecond, from a scripted caller or two clicks landing in the same
// publish interval, must not collide.
[[nodiscard]] std::uint64_t nextCaptureSequence() noexcept {
    static std::atomic<std::uint64_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

[[nodiscard]] std::string windowName(rta::dsp::WindowType type) {
    return std::string(rta::dsp::toString(type));
}

// No toString() exists for these two enums anywhere in core (unlike
// WindowType/ChannelRole) -- small local helpers, same shape as
// rta::platform::toString(ChannelRole) one layer down.
[[nodiscard]] std::string averagingName(rta::dsp::Averaging averaging) {
    switch (averaging) {
        case rta::dsp::Averaging::Linear: return "Linear";
        case rta::dsp::Averaging::Exponential: return "Exponential";
    }
    return "Exponential";
}

[[nodiscard]] std::string transferAveragingName(rta::dsp::TransferAveraging averaging) {
    switch (averaging) {
        case rta::dsp::TransferAveraging::Fifo: return "Fifo";
        case rta::dsp::TransferAveraging::Exponential: return "Exponential";
    }
    return "Fifo";
}

[[nodiscard]] std::int64_t nowUnixMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

std::vector<float> degToRadPhase(std::span<const float> phaseDeg) {
    std::vector<float> out(phaseDeg.size());
    for (std::size_t i = 0; i < phaseDeg.size(); ++i) {
        out[i] = radFromDeg(phaseDeg[i]);
    }
    return out;
}

std::string nextCaptureId() {
    return "capture-" + std::to_string(nextCaptureSequence());
}

std::optional<Trace> traceFromSnapshot(const rta::measure::Snapshot& snapshot,
                                       rta::view::PaneView pane,
                                       const rta::measure::CaptureConfig& config,
                                       std::string deviceName,
                                       std::string channelRoles) {
    CaptureMeta meta;
    meta.id = nextCaptureId();
    meta.capturedAtUnixMs = nowUnixMs();
    meta.deviceName = std::move(deviceName);
    meta.channelRoles = std::move(channelRoles);
    meta.sampleRate = snapshot.sampleRate;
    meta.fftSize = static_cast<int>(snapshot.fftSize);
    meta.window = windowName(config.window);
    // calibrationOffsetDb/calibrationUnit: left at CaptureMeta{}'s own
    // defaults (0.0f / LevelUnit::DbFs) on every path below -- owner
    // decision C3, this header's own comment.

    if (pane == rta::view::PaneView::Rta) {
        // spectrumDb carries no averaging-count or delay of its own (it is
        // the raw per-bin RTA spectrum, not a dual-FFT transfer estimate) --
        // 0.0/0 here are not a lossy stand-in for a real number, there is no
        // real number this path could report instead.
        meta.averagingType = averagingName(config.averaging);
        meta.averagingDepth = 0;
        meta.effectiveAverages = 0.0;
        meta.appliedDelaySamples = 0;
        return Trace::make(meta, snapshot.spectrumDb);
    }

    if (pane == rta::view::PaneView::Transfer) {
        // Checked independently of whether `transfer` itself is populated --
        // an inconsistent snapshot (a test fixture, or a future producer bug)
        // must still refuse on the flag alone (T4's own mutant: dropping
        // this check must go RED against exactly that fixture shape).
        if (!snapshot.hasReference || !snapshot.transfer.has_value()) {
            return std::nullopt;
        }
        const auto& tf = *snapshot.transfer;
        meta.averagingType = transferAveragingName(config.transferAveraging);
        // Fix round 1, LOW F3: `transferFifoDepth` (Analyser.h) is "Fifo
        // only" by DualFftEngine::Config's own comment -- recording it when
        // the engine is actually running Exponential would name a frame
        // count that averaging mode never uses. Exponential's own knob is
        // `Analyser::Config::timeConstantSeconds` (Analyser.cpp's
        // `toDualConfig`: `dualConfig.timeConstantSeconds = config.
        // timeConstantSeconds`), a SECONDS value with no integer "depth" to
        // put here -- `CaptureConfig` does not carry it (T4's scope named
        // only the four fields this function reads), so 0 is recorded, not
        // a lossy truncation of a value in the wrong unit.
        meta.averagingDepth = config.transferAveraging == rta::dsp::TransferAveraging::Fifo
                                  ? static_cast<int>(config.transferFifoDepth)
                                  : 0;
        meta.effectiveAverages = tf.effectiveAverages;
        meta.appliedDelaySamples = tf.appliedDelaySamples;

        auto trace = Trace::make(meta, tf.magnitudeDb);
        if (!trace.has_value()) return std::nullopt;
        if (!trace->setPhase(degToRadPhase(tf.phaseDeg))) return std::nullopt;
        // Never a zero-filled stand-in (Trace.h:17-19's own rule): copy the
        // optional itself, not `.value_or(...)`, so absence survives this
        // hop exactly like AnalyserPublish.cpp's own makeTransferBlock keeps
        // it absent.
        if (tf.coherence.has_value()) {
            if (!trace->setCoherence(*tf.coherence)) return std::nullopt;
        }
        return trace;
    }

    // Spl/Xover: neither produces a per-bin curve to freeze (research C2).
    // The caller disables the STORE button for these panes; this is the
    // defensive second line, never the only guard.
    return std::nullopt;
}

std::string channelRolesSummary(const rta::platform::ChannelConfig& config) {
    std::string out;
    for (int ch = 0; ch < rta::platform::kMaxChannels; ++ch) {
        const auto role = config.role(ch);
        if (role == rta::platform::ChannelRole::Unused) continue;
        if (!out.empty()) out += ",";
        out += "ch" + std::to_string(ch) + "=" + std::string(rta::platform::toString(role));
    }
    return out;
}

std::optional<Trace> traceFromSweepIr(const rta::measure::SweepIrOutcome& outcome,
                                      std::string deviceName,
                                      std::string channelRoles) {
    if (outcome.status != rta::measure::SweepIrStatus::Ok) return std::nullopt;

    CaptureMeta meta;
    meta.id = nextCaptureId();
    meta.capturedAtUnixMs = nowUnixMs();
    meta.deviceName = std::move(deviceName);
    meta.channelRoles = std::move(channelRoles);
    meta.sampleRate = outcome.sampleRate;
    meta.fftSize = static_cast<int>(outcome.fftSize);
    // A rectangular N-point gate from two cycles of f_lo before t = 0 (record
    // sec. 7); the lead-in is the minimum window that does not corrupt the
    // skirts, not an adjustable gate.
    meta.window = "Rectangular";
    meta.averagingType = "None";
    meta.averagingDepth = 0;
    // One sweep is one estimate: nothing was averaged, so the effective count
    // is exactly 1, not an unknown.
    meta.effectiveAverages = 1.0;
    // The loop delay is already inside the phase (t = 0 is the loop peak);
    // nothing was removed, so there is nothing for a consumer to reconcile.
    meta.appliedDelaySamples = 0;

    // A magnitude floor, so that an exactly-zero bin is -200 dB, not -inf.
    // 1e-10 is far below any bin of a sweep at -12 dBFS.
    std::vector<float> magnitudeDb(outcome.bins.size());
    std::vector<float> phase(outcome.bins.size());
    for (std::size_t k = 0; k < outcome.bins.size(); ++k) {
        magnitudeDb[k] = 20.0f * std::log10(std::max(std::abs(outcome.bins[k]), 1.0e-10f));
        phase[k] = std::arg(outcome.bins[k]);
    }

    auto trace = Trace::make(meta, std::move(magnitudeDb));
    if (!trace.has_value()) return std::nullopt;
    if (!trace->setPhase(std::move(phase))) return std::nullopt;
    // No setCoherence: absent, never zero-filled (Trace.h:17-19).
    return trace;
}

}  // namespace rta::trace
