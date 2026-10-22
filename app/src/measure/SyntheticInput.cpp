// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. JUCE (juce::Thread only). See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.4, §6 Wave D.
#include "measure/SyntheticInput.h"

#include "measure/Levels.h"
#include "measure/SyntheticPacing.h"
#include "measure/ThreadStop.h"

#include <algorithm>
#include <cmath>

namespace rta::measure {

namespace {

constexpr int kChannels = 2;
constexpr int kLoopbackOutputs = SyntheticInput::kOutputChannels;

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
    , measurementPath_(config.measurementDelaySamples, config.measurementFilter, config.roomCombDelaySamples,
                       config.roomCombGain)
    , secondPath_(config.secondOutputDelaySamples, config.secondOutputFilter)
    , secondPathBlock_(static_cast<std::size_t>(std::max(config.bufferSize, 0)))
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

    for (auto& block : outputBlock_) {
        block.assign(static_cast<std::size_t>(std::max(config.bufferSize, 0)), 0.0f);
    }

    // Message thread, before the thread body starts: satisfies the
    // precondition CaptureBus::prepare() documents (no concurrent writer).
    // No capacity argument: bus's rings were already sized once, at its own
    // construction (rta::platform::kFixedRingCapacitySamples).
    bus_.prepare(config.sampleRate, kChannels);
    bus_.setActive(true);

    // OutputEngine::prepare's precondition (OutputEngine.h): the device
    // thread is not inside render() and the message thread is not inside
    // setSource(). Both hold -- setSyntheticMode stopped the device first,
    // and our own thread starts on the line below. Two output channels
    // (ALIGN-UI T0): one mono stimulus, gated per output; `soloOutput(engine,
    // 0)` opens the channel-0 route exactly as before, channel 1 is the sub.
    if (config.loopback != nullptr) {
        config.loopback->prepare(config.sampleRate, kLoopbackOutputs);
    }

    startThread();
}

SyntheticInput::~SyntheticInput() {
    bus_.setActive(false);
    // Trap T-1: belt and braces even though the owner's member-declaration
    // order also guarantees this thread stops before bus_ is torn down.
    // Never force-killed (ThreadStop.h): runBody() does one block of work per
    // iteration and wait(waitMs_) is woken by the notify() in the join.
    stopThreadNoKill(*this);
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

    // Deadline pacing (SyntheticPacing.h), on the monotonic hi-res counter.
    const double blockMs = config_.sampleRate > 0.0
                               ? 1000.0 * static_cast<double>(block_.size()) / config_.sampleRate
                               : static_cast<double>(waitMs_);
    constexpr double kMaxLagMs = 500.0;
    double dueMs = juce::Time::getMillisecondCounterHiRes();

    // Back-pressure. A real device cannot be slowed down, a synthetic one can, and a
    // refused ring write is silent data loss: on a loaded machine the analysis thread
    // fell more than the ring's 1.36 s behind, every refused block shifted the capture's
    // timeline, and a sweep analysed it anyway (measured: 39424 frames dropped in one
    // sweep, then "arrival does not fit"). So hold the NEXT block until its ring has
    // room. Bounded: a bus nobody drains (no consumer attached) must not stall the feed
    // for ever, so after one timeout it pushes and drops as before until room returns.
    const std::size_t blockSamples = block_.size();
    const auto ringsHaveRoom = [&] {
        for (int ch = 0; ch < kChannels; ++ch) {
            if (bus_.config().role(ch) == rta::platform::ChannelRole::Unused) continue;
            const auto* ring = bus_.ring(ch);
            if (ring != nullptr && ring->availableToWrite() < blockSamples) return false;
        }
        return true;
    };
    constexpr double kMaxBackPressureMs = 3000.0;
    bool consumerStalled = false;

    while (!threadShouldExit()) {
        if (ringsHaveRoom()) {
            consumerStalled = false;
        } else if (!consumerStalled) {
            const double giveUpMs = juce::Time::getMillisecondCounterHiRes() + kMaxBackPressureMs;
            while (!threadShouldExit() && !ringsHaveRoom() &&
                   juce::Time::getMillisecondCounterHiRes() < giveUpMs) {
                wait(1);
            }
            consumerStalled = !ringsHaveRoom();
        }
        if (threadShouldExit()) break;
        // Always render the loopback, even when it is quiescent: render() is
        // what advances `renderedSamples()` (LOCATE's settle clock) and
        // publishes the engine's Idle state (`sourceIsQuiescent()`), and the
        // device callback that normally drives both is stopped in this mode.
        // RACE, harmless by design: render() can see armed == false (the
        // engine is Idle and wrote nothing) and armSource() can land on the
        // message thread before sourceIsQuiescent() is read below. The
        // verdict then says "armed" for a block the loopback did not fill:
        // one silent block goes onto the bus, uncounted. It does not matter
        // because LOCATE's settle clock is renderedSamples(), which render()
        // advanced regardless, and the capture it arms later starts past the
        // 480-sample settle window, so that one block cannot be in it.
        // Quiescence is sampled AFTER the render so the verdict and the
        // block it is about come from the same engine state: a block rendered
        // while armed is used even if a disarm lands a moment later.
        bool useLoopback = false;
        const std::size_t n = block_.size();
        if (config_.loopback != nullptr) {
            float* outs[kLoopbackOutputs] = {outputBlock_[0].data(), outputBlock_[1].data()};
            config_.loopback->render(outs, kLoopbackOutputs, static_cast<int>(n));
            useLoopback = !config_.loopback->sourceIsQuiescent();
        }
        if (useLoopback) {
            // Reference = the lowest ROUTED output's block: the console's
            // own signal, whichever output the wizard soloed. None routed
            // (a gate still ramping out) reads output 0.
            int referenceOutput = 0;
            for (int c = 0; c < kLoopbackOutputs; ++c) {
                if (config_.loopback->role(c) == rta::platform::OutputRole::Routed) {
                    referenceOutput = c;
                    break;
                }
            }
            std::copy(outputBlock_[referenceOutput].begin(), outputBlock_[referenceOutput].end(),
                      block_.begin());
        } else {
            if (pink_) {
                pink_->render(block_);
            } else {
                sine_->render(block_);
            }
            std::copy(block_.begin(), block_.end(), outputBlock_[0].begin());
            std::fill(outputBlock_[1].begin(), outputBlock_[1].end(), 0.0f);
        }

        // Impairments land on the MEASUREMENT copy only -- block_ (the
        // channel written as Reference below) stays clean. Each output has
        // its own path; the microphone hears their sum. An unrouted output
        // is exact zeros through a zero-state path, so a single-output run
        // is bit-identical to the one-path code this replaced. See
        // SyntheticImpairment.h for why the delay's history and the noise
        // state are members.
        measurementPath_.process(outputBlock_[0], measurementBlock_);
        secondPath_.process(outputBlock_[1], secondPathBlock_);
        for (std::size_t i = 0; i < n; ++i) {
            measurementBlock_[i] += secondPathBlock_[i];
        }

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

        const SyntheticPace pace =
            nextSyntheticPace(dueMs, juce::Time::getMillisecondCounterHiRes(), blockMs, kMaxLagMs);
        dueMs = pace.dueMs;
        if (pace.waitMs > 0) wait(pace.waitMs);
    }
}

}  // namespace rta::measure
