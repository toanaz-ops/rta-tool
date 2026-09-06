// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Record §11 T11 / plan R6: two transfer functions sharing one reference
// channel receive EQUAL paired-hop counters (they were driven from the same
// drainPaired() call, over the same reference peek); two transfer functions
// naming DIFFERENT reference channels receive INDEPENDENT counters. This is
// exercised through a REAL AnalysisThread over a REAL CaptureBus -- the pure
// routing DECISION is already covered OFF-build in test_routing_plan.cpp;
// what only a live thread can prove is that drainPaired's grouping actually
// behaves this way once real rings, real timing and the poll loop are in the
// picture.
//
// Feeds the bus directly through pushFromCallback -- the same entry point
// SyntheticInput uses -- rather than via SyntheticInput itself: that class is
// built for exactly one reference/one measurement channel (measure/
// SyntheticInput.h's own class comment), and this test needs three or four
// channels with independent routing, which is exactly the scenario SyntheticInput
// predates.

#include <catch2/catch_test_macros.hpp>

#include "measure/AnalysisThread.h"

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

using rta::measure::Analyser;
using rta::measure::AnalysisThread;
using rta::platform::CaptureBus;
using rta::platform::ChannelRole;

namespace {

// A small, fast Analyser::Config: real fftSize/hopSize would need thousands
// of samples per hop to clear even one frame, and this test only needs
// pushPair to have been CALLED a countable number of times, not a gated
// transfer function -- MTW is left ON (the default) since B2 does not touch
// mtwEnabled and the point here is the DRAIN, not the DSP it feeds.
Analyser::Config fastConfig() {
    Analyser::Config config;
    config.fftSize = 64;
    config.hopSize = 16;
    config.sampleRate = 48000.0;
    config.transferFifoDepth = 4;
    config.mtwEnabled = false;  // keep the test fast; routing is what's under test
    return config;
}

// Pushes `blocks` blocks of `samplesPerBlock` non-zero samples into every
// channel named in `toneChannels`, and SILENCE into every other channel
// below `channelsInCallback` -- through the SAME pushFromCallback entry
// point the real audio callback and SyntheticInput both use.
//
// `channelsInCallback` is deliberately a caller-controlled argument, not
// always the bus's full channel count: `pushFromCallback` writes to every
// channel index below it (silence counts as a write -- it still advances
// that ring), so a call with `channelsInCallback = 3` on a 4-channel bus
// leaves channel 3 completely untouched, exactly like a real device that
// only ever reports 3 channels never advances a 4th ring at all. That is
// the tool the "independent references" test below uses to give one
// route's measurement channel far fewer available samples than the other's,
// without touching AnalysisThread at all.
void pushBlocks(CaptureBus& bus, std::span<const int> toneChannels, int channelsInCallback,
                int samplesPerBlock, int blocks) {
    std::vector<float> silence(static_cast<std::size_t>(samplesPerBlock), 0.0f);
    std::vector<float> tone(static_cast<std::size_t>(samplesPerBlock), 0.1f);
    std::vector<const float*> ptrs(static_cast<std::size_t>(channelsInCallback), silence.data());
    for (int ch : toneChannels) {
        if (ch >= 0 && ch < channelsInCallback) {
            ptrs[static_cast<std::size_t>(ch)] = tone.data();
        }
    }
    for (int b = 0; b < blocks; ++b) {
        bus.pushFromCallback(ptrs.data(), channelsInCallback, samplesPerBlock);
    }
}

// Polls `thread`'s routeHopCount(routeIndex) until it reaches `target` or
// `timeoutMs` elapses. The analysis thread polls its bus every 10 ms
// (kPollMs in AnalysisThread.cpp); this only ever needs to wait a handful of
// polls once the samples are already sitting in the rings.
bool waitForHops(const AnalysisThread& thread, int routeIndex, std::uint64_t target,
                 int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        if (thread.routeHopCount(routeIndex) >= target) return true;
        juce::Thread::sleep(5);
    }
    return thread.routeHopCount(routeIndex) >= target;
}

}  // namespace

TEST_CASE("Two transfer functions sharing one reference get equal hop counters",
          "[routing_live]") {
    CaptureBus bus(4096);
    // ch0 = Reference (tf 0, the default); ch1, ch2 = Measurement (tf 0,
    // the default) -- both name ch0 as their reference (task B1's lookup:
    // a Reference-role channel whose OWN tfIndex matches).
    REQUIRE(bus.config().setRole(0, ChannelRole::Reference));
    REQUIRE(bus.config().setRole(1, ChannelRole::Measurement));
    REQUIRE(bus.config().setRole(2, ChannelRole::Measurement));
    bus.prepare(48000.0, 3);
    bus.setActive(true);

    AnalysisThread thread(bus, fastConfig());

    const std::array<int, 3> allChannels{0, 1, 2};
    pushBlocks(bus, allChannels, 3, 16, 20);

    REQUIRE(waitForHops(thread, 0, 1, 2000));
    REQUIRE(waitForHops(thread, 1, 1, 2000));

    // Give the thread a little more time to settle on a stable pair of
    // counts (both routes are drained together, in the same drainPaired()
    // call, over the same reference peek -- record §11 T11's "identical
    // reference hops").
    juce::Thread::sleep(50);
    const auto first = thread.routeHopCount(0);
    const auto second = thread.routeHopCount(1);
    CHECK(first > 0);
    CHECK(first == second);
}

TEST_CASE("Two transfer functions naming different references get independent counters",
          "[routing_live]") {
    CaptureBus bus(4096);
    // ch0, ch1 = Reference; ch1 tagged tf 1. ch2 = Measurement, tf 0 (->
    // ch0). ch3 = Measurement, tf 1 (-> ch1). Two fully independent
    // transfer functions.
    REQUIRE(bus.config().setRole(0, ChannelRole::Reference));
    REQUIRE(bus.config().setRole(1, ChannelRole::Reference));
    REQUIRE(bus.config().setTransferFunction(1, 1));
    REQUIRE(bus.config().setRole(2, ChannelRole::Measurement));
    REQUIRE(bus.config().setRole(3, ChannelRole::Measurement));
    REQUIRE(bus.config().setTransferFunction(3, 1));
    bus.prepare(48000.0, 4);
    bus.setActive(true);

    AnalysisThread thread(bus, fastConfig());

    // tf 0's pair (ch0, ch2) gets 40 blocks over a 3-CHANNEL callback --
    // channel 3 is never even mentioned, so its ring gains nothing here.
    // tf 1's pair (ch1, ch3) then gets only 5 blocks, over the full
    // 4-channel width (channels 0/2 get topped up with harmless silence,
    // already having plenty). Channel 3 ends up with exactly 5 blocks
    // available -- far fewer than channels 0/1/2's 45 -- so tf 1's route is
    // capped by its OWN measurement channel, not by tf 0's reference. If the
    // drain were one shared budget across every reference (the paired-drain
    // defect one level up), tf 1 would be pulled up to tf 0's count instead
    // of staying capped at its own.
    const std::array<int, 2> tf0Channels{0, 2};
    const std::array<int, 2> tf1Channels{1, 3};
    pushBlocks(bus, tf0Channels, /*channelsInCallback=*/3, 16, 40);
    pushBlocks(bus, tf1Channels, /*channelsInCallback=*/4, 16, 5);

    REQUIRE(waitForHops(thread, 0, 20, 2000));
    REQUIRE(waitForHops(thread, 1, 1, 2000));

    juce::Thread::sleep(50);
    const auto tf0Hops = thread.routeHopCount(0);
    const auto tf1Hops = thread.routeHopCount(1);
    CHECK(tf0Hops >= 20);
    CHECK(tf1Hops >= 1);
    CHECK(tf1Hops <= 5);
    // Independent: tf 0 has far more samples available and must show it,
    // not be capped down to tf 1's much smaller count.
    CHECK(tf0Hops > tf1Hops);
}
