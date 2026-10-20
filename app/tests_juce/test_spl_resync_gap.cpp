// SPDX-License-Identifier: AGPL-3.0-or-later
//
// PR #78 fix round 2 (MEDIUM): a role assigned while an SPL log runs raises the
// bus's resync gate with NO epoch change, so the SPL feed's epoch freeze does
// not cover it. The gate refuses frames and the consumer's drain discards each
// ring's backlog; both are lost time. Before this fix the feed derived
// BlockFlag::Gap only from CaptureBus::dropCount, so the Leq block closed short
// with no Gap and every later timestamp in the log was silently early.
//
// A real AnalysisThread over a real CaptureBus (the test_spl_drain.cpp shape);
// this test thread plays the audio callback, because the resync's
// acknowledgement is a callback observing the gate.
#include <catch2/catch_test_macros.hpp>

#include "measure/AnalysisThread.h"

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

using rta::measure::Analyser;
using rta::measure::AnalysisThread;
using rta::measure::SplConfig;
using rta::platform::CaptureBus;
using rta::platform::ChannelRole;

namespace {

Analyser::Config fastConfig() {
    Analyser::Config config;
    config.fftSize = 64;
    config.hopSize = 16;
    config.sampleRate = 48000.0;
    config.transferFifoDepth = 4;
    config.mtwEnabled = false;
    return config;
}

SplConfig splConfig() {
    SplConfig config;
    config.blockSeconds = 64.0 / 48000.0;  // 64 samples = 4 hops
    config.metrics.push_back(rta::measure::SplMetricSpec{
        "L", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, 4});
    return config;
}

/// One audio callback: `channels` channels of `samples` samples, 0.1 on each.
void callback(CaptureBus& bus, int channels, int samples) {
    const std::vector<float> tone(static_cast<std::size_t>(samples), 0.1f);
    std::vector<const float*> ptrs(static_cast<std::size_t>(channels), tone.data());
    bus.pushFromCallback(ptrs.data(), channels, samples);
}

}  // namespace

TEST_CASE("a role assigned while an SPL log runs puts a Gap on the affected block",
          "[spl_resync_gap]") {
    CaptureBus bus(4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Reference));
    REQUIRE(bus.config().setRole(1, ChannelRole::Measurement));
    bus.prepare(48000.0, 3);
    bus.setActive(true);

    AnalysisThread thread(bus, fastConfig());
    const std::array<int, 1> logged{1};
    thread.enableSplLogging(splConfig(), logged);

    // Steady state: blocks close, and nothing has been lost yet.
    for (int i = 0; i < 400 && thread.splBlockCount(1) < 4; ++i) {
        callback(bus, 3, 16);
        juce::Thread::sleep(2);
    }
    REQUIRE(thread.splBlockCount(1) >= 4);
    REQUIRE_FALSE(rta::meter::hasFlag(thread.splFlagsSeen(1), rta::meter::BlockFlag::Gap));
    REQUIRE(thread.splDroppedSamples(1) == 0);

    // The operator assigns a third mic while logging. No epoch change.
    REQUIRE(bus.config().setRole(2, ChannelRole::Measurement));

    // Keep the callbacks coming: the gate refuses them, and the first one that
    // sees it up is what lets the consumer drain. Then let blocks close again.
    const auto before = thread.splBlockCount(1);
    for (int i = 0; i < 600 && (bus.resyncPending() || thread.splBlockCount(1) < before + 2 ||
                                bus.resyncLostFrames() == 0);
         ++i) {
        callback(bus, 3, 16);
        juce::Thread::sleep(2);
    }
    REQUIRE_FALSE(bus.resyncPending());
    REQUIRE(bus.resyncLostFrames() > 0);
    juce::Thread::sleep(50);

    INFO("resyncLostFrames = " << bus.resyncLostFrames());
    INFO("splDroppedSamples(1) = " << thread.splDroppedSamples(1));
    CHECK(rta::meter::hasFlag(thread.splFlagsSeen(1), rta::meter::BlockFlag::Gap));
    CHECK(thread.splDroppedSamples(1) > 0);
    // Never more than the bus lost: the log reports the bus's own count.
    CHECK(thread.splDroppedSamples(1) <= bus.resyncLostFrames() + bus.dropCount(1));
}
