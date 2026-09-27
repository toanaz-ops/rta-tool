// SPDX-License-Identifier: AGPL-3.0-or-later
#include "trace/CaptureConverter.h"

#include <atomic>
#include <chrono>
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
                                       const rta::measure::Analyser::Config& config,
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
        meta.averagingDepth = static_cast<int>(config.transferFifoDepth);
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

}  // namespace rta::trace
