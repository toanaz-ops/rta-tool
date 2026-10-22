// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Shared rig for the AlignRunner tests (ALIGN-UI lane wave A, plan T2): a
// standalone rta::platform::OutputEngine rendered by hand (the
// EqVerifyRunnerFixture.h pattern) plus scripted snapshots -- no device, no
// JUCE. Header-only and JUCE-free.
//
// THE PHYSICS OF THE FIXTURE is what makes the dwell testable. `latest()`
// serves a snapshot that depends on which outputs are ROUTED, and it turns over
// only after depth * fftSize samples have rendered SINCE THE ROUTING LAST
// CHANGED -- the live transfer FIFO still holds the previous output's frames
// until then. A runner that submits the first snapshot, or counts its dwell
// from the arm instead of from each step's own solo, therefore stores one
// output's response under the other's name, and the traces say so.
//
// The pair is the plan's own: output 0 an LR4 high-pass at 100 Hz after a
// delay of 4 samples, output 1 an LR4 low-pass at 100 Hz after 100, so the
// low-pass side arrives 96 samples = 2.000 ms later. The analytic prototypes
// (s = j f/fc, D = s^2 + sqrt(2) s + 1, HP = s^4/D^2, LP = 1/D^2) differ by
// exactly N*90 = 360 degrees at every frequency, so LR-4 NOT INVERTED has an
// expected offset of 0 and the only structure left is the delay.
#pragma once

#include "measure/AlignRunner.h"
#include "measure/Analyser.h"
#include "measure/MeasurementDrive.h"
#include "measure/Snapshot.h"
#include "trace/CaptureConverter.h"
#include "trace/TraceLibrary.h"

#include "rta/platform/OutputEngine.h"

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace alignfixture {

inline constexpr int kFft = 4096;
inline constexpr std::size_t kBins = static_cast<std::size_t>(kFft) / 2 + 1;
inline constexpr double kFs = 48000.0;
inline constexpr int kBlock = 512;
inline constexpr int kChannels = 2;
inline constexpr double kCrossoverHz = 100.0;
inline constexpr int kHighDelaySamples = 4;
inline constexpr int kLowDelaySamples = 100;

using rta::measure::AlignEnvironment;
using rta::measure::Snapshot;
using rta::measure::SnapshotPtr;
using rta::measure::TransferBlock;

/// Which routed set a snapshot describes: 1 = output 0 only, 2 = output 1
/// only, 3 = both, 0 = nothing routed yet (a flat 0 dB, zero phase room).
[[nodiscard]] inline std::complex<double> response(int routed, double hz) {
    using namespace std::complex_literals;
    const std::complex<double> s = 1i * (hz / kCrossoverHz);
    const std::complex<double> d = s * s + std::sqrt(2.0) * s + 1.0;
    const auto delay = [hz](int samples) {
        return std::polar(1.0, -2.0 * std::numbers::pi * hz * static_cast<double>(samples) / kFs);
    };
    const std::complex<double> hp = (s * s * s * s) / (d * d) * delay(kHighDelaySamples);
    const std::complex<double> lp = 1.0 / (d * d) * delay(kLowDelaySamples);
    switch (routed) {
        case 1: return hp;
        case 2: return lp;
        case 3: return hp + lp;
        default: return 1.0;
    }
}

[[nodiscard]] inline SnapshotPtr makeSnapshot(int routed, int fftSize = kFft, double sampleRate = kFs) {
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->sampleRate = sampleRate;
    snapshot->fftSize = static_cast<std::size_t>(fftSize);
    snapshot->hasReference = true;
    TransferBlock transfer;
    const std::size_t bins = static_cast<std::size_t>(fftSize) / 2 + 1;
    for (std::size_t k = 0; k < bins; ++k) {
        const std::complex<double> h = response(routed, static_cast<double>(k) * sampleRate / fftSize);
        transfer.magnitudeDb.push_back(static_cast<float>(std::max(20.0 * std::log10(std::abs(h)), -200.0)));
        transfer.phaseDeg.push_back(static_cast<float>(std::arg(h) * 180.0 / std::numbers::pi));
    }
    transfer.coherence = std::vector<float>(bins, 1.0f);
    transfer.effectiveAverages = 100.0;
    snapshot->transfer = std::move(transfer);
    return snapshot;
}

struct Rig {
    rta::trace::TraceLibrary library;
    rta::platform::OutputEngine engine;
    rta::measure::AlignRunner runner;

    AlignEnvironment env{ /*synthetic*/ false, /*deviceRunning*/ true, /*captureBusy*/ false, kChannels };
    rta::measure::CaptureConfig config{};
    double nowMs = 1000.0;
    bool nullSnapshot = false;
    std::optional<SnapshotPtr> forcedSnapshot;  ///< overrides the physics (refusal rows)
    int freezes = 0;
    /// Replaces the real conversion (refusal rows: a trace the wizard cannot use).
    std::function<std::optional<rta::trace::Trace>(const Snapshot&)> freezeOverride;
    /// H7 T13: what the host's `drive` answers when the rig was built with one.
    rta::measure::MeasurementDrive driveValue;

    std::uint64_t totalRendered = 0;
    int signature = 0;
    int previousSignature = 0;
    std::uint64_t changedAt = 0;

    explicit Rig(std::optional<rta::measure::MeasurementDrive> drive = std::nullopt) {
        engine.prepare(kFs, kChannels);
        rta::measure::AlignHost host;
        host.environment = [this] { return env; };
        host.latest = [this]() -> SnapshotPtr {
            if (nullSnapshot) return nullptr;
            if (forcedSnapshot.has_value()) return *forcedSnapshot;
            const bool turnedOver = totalRendered - changedAt >= turnoverSamples();
            return makeSnapshot(turnedOver ? signature : previousSignature);
        };
        host.captureConfig = [this] { return config; };
        // The REAL conversion, as MainComponent's host does it: what the
        // runner stores is what STORE would have stored.
        host.freeze = [this](const Snapshot& s) {
            ++freezes;
            if (freezeOverride) return freezeOverride(s);
            return rta::trace::traceFromSnapshot(s, rta::view::PaneView::Transfer, config, "TEST",
                                                 "ch0=Measurement,ch1=Reference");
        };
        host.nowMs = [this] { return nowMs; };
        host.timeLabel = [] { return std::string("12:34:56"); };
        if (drive.has_value()) {
            driveValue = *drive;
            host.drive = [this] { return driveValue; };
        }
        runner.attach(engine, library, std::move(host));
    }

    /// depth * fftSize rendered samples: the physical turnover of the live
    /// transfer FIFO (Analyser.h CaptureConfig::transferFifoDepth).
    [[nodiscard]] std::uint64_t turnoverSamples() const {
        return static_cast<std::uint64_t>(config.transferFifoDepth) * static_cast<std::uint64_t>(kFft);
    }

    void answerAll(rta::measure::Source highPass = rta::measure::Source::Main) {
        auto& a = runner.answers();
        a.highPass = highPass;
        a.topology = rta::measure::Topology{ rta::measure::CrossoverFamily::LinkwitzRiley, 4 };
        a.inversion = rta::measure::ProcessorInversion::No;
        a.seedAnswered = true;
        a.seedHz = std::nullopt;  // NO SEED is an answer
    }

    [[nodiscard]] int routedSignature() const {
        return (engine.role(0) == rta::platform::OutputRole::Routed ? 1 : 0) |
               (engine.role(1) == rta::platform::OutputRole::Routed ? 2 : 0);
    }

    void observeRoutes() {
        const int now = routedSignature();
        if (now == signature) return;
        previousSignature = signature;
        signature = now;
        changedAt = totalRendered;
    }

    /// Renders `blocks` blocks of kBlock samples, polling after each.
    void pump(int blocks) {
        for (int b = 0; b < blocks; ++b) {
            observeRoutes();
            renderOne();
            runner.poll();
        }
    }
    /// Renders WITHOUT polling: the engine advances, the runner does not.
    void renderOnly(int blocks) {
        for (int b = 0; b < blocks; ++b) {
            observeRoutes();
            renderOne();
        }
    }
    /// Pumps until `pred` (bounded), returning whether it held.
    template <class Pred>
    bool pumpUntil(Pred pred, int maxBlocks = 600) {
        for (int b = 0; b < maxBlocks && !pred(); ++b) pump(1);
        return pred();
    }
    /// press + run until the fit has run (bounded), then let the excitation's
    /// ramp-out finish: a real operator's next press comes seconds later, and
    /// the engine refuses a new source until it is quiescent.
    bool runToVerdict() {
        if (!runner.press()) return false;
        if (!pumpUntil([this] { return !runner.busy(); })) return false;
        pump(8);
        return true;
    }

private:
    void renderOne() {
        std::vector<std::vector<float>> buffers(kChannels, std::vector<float>(kBlock, 0.0f));
        std::vector<float*> ptrs;
        for (auto& channel : buffers) ptrs.push_back(channel.data());
        engine.render(ptrs.data(), kChannels, kBlock);
        totalRendered += kBlock;
    }
};

}  // namespace alignfixture
