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

}  // namespace

SyntheticInput::SyntheticInput(rta::platform::CaptureBus& bus, const Config& config)
    : juce::Thread("rta SyntheticInput")
    , bus_(bus)
    , config_(config)
    , waitMs_(millisecondsPerBlock(config.sampleRate, config.bufferSize))
    , block_(static_cast<std::size_t>(std::max(config.bufferSize, 0))) {
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
    while (!threadShouldExit()) {
        if (pink_) {
            pink_->render(block_);
        } else {
            sine_->render(block_);
        }

        // Same content on both channels -- see the header for why. Read-only
        // through pushFromCallback, so aliasing the two pointers is safe.
        const float* channels[kChannels] = {block_.data(), block_.data()};
        bus_.pushFromCallback(channels, kChannels, static_cast<int>(block_.size()));

        wait(waitMs_);
    }
}

}  // namespace rta::measure
