// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. JUCE (juce::Thread only). See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.4, §6 Wave D.
#pragma once

#include "rta/gen/Synthetic.h"
#include "rta/platform/CaptureBus.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace rta::measure {

/// Pushes deterministic synthetic audio into a live `CaptureBus` through the
/// SAME `pushFromCallback` entry point `rta::platform::AudioIo`'s real
/// device callback uses (plan §1.2) -- one write path, not a second one
/// that can drift from it. This is what lets `rtatool.exe` open, show a
/// moving plot, and be verified with no interface plugged in at all (plan
/// §0, item 4).
///
/// Paced in real time by `wait()` between blocks, deliberately NOT a tight
/// loop: anything downstream that assumes audio arrives no faster than it
/// is sampled (e.g. `AnalysisThread`'s publish-rate limiting) needs that to
/// stay true for a synthetic source exactly as it is for a real device.
///
/// `rta::gen::SyntheticPink` / `SyntheticSine` are the deterministic TEST
/// generators (see rta/gen/Synthetic.h) -- exact by construction and
/// bit-identical for a given seed, not the real-time product generator a
/// show plays through a loudspeaker. That distinction is the whole reason
/// this class exists as "hardware-free operation", not "signal generator
/// output".
class SyntheticInput final : public juce::Thread {
public:
    struct Config {
        enum class Source { PinkNoise, Sine };

        Source source = Source::PinkNoise;
        double sampleRate = 48000.0;
        /// Samples pushed per block, and the pacing unit: each `wait()`
        /// between pushes is `1000 * bufferSize / sampleRate` ms.
        int bufferSize = 512;
        double sineHz = 1000.0;
        /// Peak amplitude, sine-referenced -- same convention as
        /// `SyntheticSpec::amplitude` (measure/SyntheticSnapshot.h): 1.0 is
        /// a full-scale sine. Converted through Levels.h's `levelDbFs`, the
        /// one dB definition this app uses everywhere.
        double amplitude = 0.1;
        std::uint32_t seed = 0x5EEDu;
    };

    /// Calls `bus.prepare()` and `bus.setActive(true)` here, on the message
    /// thread, before the thread body ever touches `bus` -- matching the
    /// precondition `CaptureBus::prepare` documents (no concurrent writer).
    /// Starts the thread immediately; there is no separate `start()`.
    SyntheticInput(rta::platform::CaptureBus& bus, const Config& config);

    /// Same stop discipline as `AnalysisThread` (trap T-1): `stopThread`
    /// in this class's own destructor, belt and braces against the owner's
    /// member-declaration order.
    ~SyntheticInput() override;

    SyntheticInput(const SyntheticInput&) = delete;
    SyntheticInput& operator=(const SyntheticInput&) = delete;

private:
    void run() override;
    void runBody();

    rta::platform::CaptureBus& bus_;
    Config config_;
    int waitMs_;

    // Exactly one of these is engaged, per config_.source.
    std::unique_ptr<rta::gen::SyntheticPink> pink_;
    std::unique_ptr<rta::gen::SyntheticSine> sine_;

    /// One block, regenerated in place every iteration. Written into BOTH
    /// requested channels (bus.prepare(rate, 2, ...): the same content on
    /// each, since this class has no notion of measurement vs. reference --
    /// which channel is analysed as which is a `ChannelConfig` decision the
    /// device panel makes, not this class's.
    std::vector<float> block_;
};

}  // namespace rta::measure
