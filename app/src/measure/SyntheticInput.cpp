// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. JUCE (juce::Thread only). See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.4, §6 Wave D.
#include "measure/SyntheticInput.h"

#include "measure/Levels.h"

#include <algorithm>
#include <cmath>

namespace rta::measure {

namespace {

constexpr int kChannels = 2;

int millisecondsPerBlock(double sampleRate, int bufferSize) {
    if (sampleRate <= 0.0) {
        return 1;
    }
    const double ms = 1000.0 * static_cast<double>(bufferSize) / sampleRate;
    return std::max(1, static_cast<int>(std::lround(ms)));
}

// xorshift32 (see SyntheticImpairment.h) gets stuck at zero if seeded with
// zero, and never leaves zero once it lands there -- guard the input, not
// just the initial value.
std::uint32_t noiseSeedFrom(std::uint32_t configSeed) {
    const std::uint32_t offset = configSeed ^ 0x9E3779B9u;
    return offset != 0 ? offset : 0x1u;
}

}  // namespace

SyntheticInput::SyntheticInput(rta::platform::CaptureBus& bus, const Config& config)
    : juce::Thread("rta SyntheticInput")
    , bus_(bus)
    , config_(config)
    , waitMs_(millisecondsPerBlock(config.sampleRate, config.bufferSize))
    , block_(static_cast<std::size_t>(std::max(config.bufferSize, 0)))
    , measurementBlock_(static_cast<std::size_t>(std::max(config.bufferSize, 0)))
    , measurementDelay_(config.measurementDelaySamples)
    // config.seed already seeds SyntheticPink below; offset by a constant
    // rather than reuse it verbatim, so the delay's noise floor is not
    // correlated with the pink-noise generator's exact state.
    , noiseState_(noiseSeedFrom(config.seed)) {
    // The one dB conversion this app uses everywhere (Levels.h): peak
    // amplitude -> mean-square -> sine-referenced dBFS.
    const double targetDbFs = levelDbFs(config.amplitude * config.amplitude * 0.5);

    if (config.source == Config::Source::Sine) {
        sine_ = std::make_unique<rta::gen::SyntheticSine>(config.sampleRate, config.sineHz,
                                                            targetDbFs);
    } else {
        const std::size_t blockSize = std::max<std::size_t>(block_.size(), 4);
        pink_ = std::make_unique<rta::gen::SyntheticPink>(blockSize, targetDbFs, config.seed);
    }

    // Message thread, before the thread body starts: satisfies the
    // precondition CaptureBus::prepare() documents (no concurrent writer).
    // No capacity argument: bus's rings were already sized once, at its own
    // construction (rta::platform::kFixedRingCapacitySamples).
    bus_.prepare(config.sampleRate, kChannels);
    bus_.setActive(true);

    startThread();
}

SyntheticInput::~SyntheticInput() {
    bus_.setActive(false);
    // Trap T-1: belt and braces even though the owner's member-declaration
    // order also guarantees this thread stops before bus_ is torn down.
    stopThread(2000);
}

void SyntheticInput::run() {
    // Same rule as AnalysisThread (trap T-3): an exception escaping
    // juce::Thread::run() terminates the process. Neither generator's
    // render() is documented to throw (both are noexcept), but the bus
    // calls beneath prepare()/pushFromCallback are not all guaranteed
    // allocation-free at this call depth, so the guard costs nothing and
    // buys the same safety AnalysisThread has.
    try {
        runBody();
    } catch (...) {
        // Nothing downstream reads a fault from this class (it is a test
        // feed, not a device); stopping cleanly is the whole contract.
    }
}

void SyntheticInput::runBody() {
    // Peak amplitude -> RMS relative to `amplitude`, same convention as the
    // constructor's targetDbFs conversion above.
    const double noiseRms =
        config_.amplitude * std::pow(10.0, config_.measurementNoiseDb / 20.0);

    while (!threadShouldExit()) {
        if (pink_) {
            pink_->render(block_);
        } else {
            sine_->render(block_);
        }

        // Impairments land on the MEASUREMENT copy only -- block_ (the
        // channel written as Reference below) stays clean. measurementDelay_
        // and noiseState_ are members, not locals, precisely so the delay's
        // history and the noise generator's state carry across this call;
        // see SyntheticImpairment.h.
        measurementDelay_.process(block_, measurementBlock_);

        // kNoiseOffDb is a genuine OFF, not just a very small level: below it
        // (or at it -- the default) the generator is not called at all, so a
        // caller who never touches measurementNoiseDb gets bit-identical
        // channels exactly as before this class had an impairment knob.
        // "Inaudibly small" is not the same promise as "unchanged".
        if (config_.measurementNoiseDb > Config::kNoiseOffDb) {
            addNoise(measurementBlock_, static_cast<float>(noiseRms), noiseState_);
        }

        // By ROLE, not by a fixed channel index: bus_.config() is the
        // device-panel-owned table of "what is channel N for". A fixed
        // "channel 0 is clean" convention would produce a *negative* measured
        // delay the moment somebody swaps the roles there, with nothing on
        // screen to explain why. Default every channel to the clean block so
        // an Unused or Reference-role channel gets it; only a Measurement-
        // role channel is overridden with the impaired one.
        const float* channels[kChannels] = {block_.data(), block_.data()};
        for (int ch = 0; ch < kChannels; ++ch) {
            if (bus_.config().role(ch) == rta::platform::ChannelRole::Measurement) {
                channels[ch] = measurementBlock_.data();
            }
        }
        bus_.pushFromCallback(channels, kChannels, static_cast<int>(block_.size()));

        wait(waitMs_);
    }
}

}  // namespace rta::measure
