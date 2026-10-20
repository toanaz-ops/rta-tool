// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Audit prj010-fable-audit-20261021 (high), behavioural half. CaptureBus::
// prepare() no longer empties the rings (that wrote the consumer's read index
// from the device thread); the consumer drains them itself on the tick it sees
// the epoch change. This drives a REAL AnalysisThread over a REAL CaptureBus
// and proves the drain happens: samples written under the previous session
// never reach an analyser of the new one.
//
// The structural half -- prepare() contains no reset( -- is G1 in
// app/tests/test_capture_bus_guard.cpp. Neither half alone is enough: G1 says
// the producer stopped touching the index, this says somebody still empties
// the rings.
//
// Why a HALF hop is pushed: the analysis thread consumes only whole hops, so a
// half hop is a leftover it cannot consume on its own. Under a mutant that
// skips the drain, those 8 samples sit in the ring forever, which is exactly
// what the test then fails on.
#include <catch2/catch_test_macros.hpp>

#include "measure/AnalysisThread.h"

#include <juce_core/juce_core.h>

#include <vector>

using rta::measure::Analyser;
using rta::measure::AnalysisThread;
using rta::platform::CaptureBus;
using rta::platform::ChannelRole;

namespace {

constexpr int kHop = 16;
constexpr int kHalfHop = kHop / 2;

Analyser::Config fastConfig() {
    Analyser::Config config;
    config.fftSize = 64;
    config.hopSize = kHop;
    config.sampleRate = 48000.0;
    config.transferFifoDepth = 4;
    config.mtwEnabled = false;
    return config;
}

void pushSamples(CaptureBus& bus, int count, float value) {
    const std::vector<float> data(static_cast<std::size_t>(count), value);
    const float* channels[1] = {data.data()};
    bus.pushFromCallback(channels, 1, count);
}

bool waitForRingToEmpty(CaptureBus& bus, int timeoutMs) {
    const auto deadline =
        juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        if (bus.ring(0)->availableToRead() == 0) return true;
        juce::Thread::sleep(5);
    }
    return bus.ring(0)->availableToRead() == 0;
}

}  // namespace

TEST_CASE("after an epoch change the consumer starts from an empty ring",
          "[capture_bus_epoch_drain]") {
    CaptureBus bus(4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    bus.prepare(48000.0, 1);
    bus.setActive(true);

    AnalysisThread thread(bus, fastConfig());
    juce::Thread::sleep(50);  // thread is up and idling in its poll

    // A half hop from the OLD session. The thread cannot consume it.
    pushSamples(bus, kHalfHop, 1.0f);
    juce::Thread::sleep(50);
    REQUIRE(bus.ring(0)->availableToRead() == static_cast<std::size_t>(kHalfHop));

    // The device reconfigures. prepare() must leave the ring alone...
    bus.prepare(96000.0, 1);
    bus.setActive(true);

    // ...and the consumer must empty it on its next tick.
    INFO("ring(0) still holds " << bus.ring(0)->availableToRead()
         << " samples from the previous session 2 s after the epoch changed");
    CHECK(waitForRingToEmpty(bus, 2000));

    // The property that matters: a half hop of NEW-session samples does not
    // complete a hop together with stale ones. A leftover stale half hop plus
    // these eight would be one whole hop, consumed and pushed into a fresh
    // analyser as a frame that splices two sessions.
    pushSamples(bus, kHalfHop, 2.0f);
    juce::Thread::sleep(100);
    CHECK(bus.ring(0)->availableToRead() == static_cast<std::size_t>(kHalfHop));
}
