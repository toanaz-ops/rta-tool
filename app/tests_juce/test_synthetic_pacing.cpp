// SPDX-License-Identifier: AGPL-3.0-or-later
//
// SyntheticInput's pacing and back-pressure (align IR load flake, PR #90): the deadline
// pace function in closed form, and the feed's behaviour against a full ring.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "measure/SyntheticInput.h"
#include "measure/SyntheticPacing.h"
#include "rta/platform/CaptureBus.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>
#include <functional>
#include <memory>

using rta::measure::SyntheticInput;
using rta::platform::CaptureBus;
using rta::platform::ChannelRole;

namespace {
constexpr int kBlock = 512;  // SyntheticInput::Config::bufferSize default
constexpr int kMeasurementCh = 0;
constexpr int kReferenceCh = 1;

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        if (predicate()) return true;
        juce::Thread::sleep(5);
    }
    return predicate();
}

void assignRoles(CaptureBus& bus) {
    REQUIRE(bus.config().setRole(kMeasurementCh, ChannelRole::Measurement));
    REQUIRE(bus.config().setRole(kReferenceCh, ChannelRole::Reference));
}
}  // namespace

// ---- Deadline pacing (SyntheticPacing.h) -------------------------------------------
// The align-IR load flake: a feed that sleeps blockMs after each block, and wakes up
// to a timer tick late, runs slower than real time, so wall-clock timeouts in front of
// it expire on a healthy run. A scheduler model with a fixed lateness per sleep makes
// the contrast exact, so no wall clock is involved.
namespace {
struct PacingRun {
    double nowMs;
    double dueMs;
};
PacingRun simulatePacing(int blocks, double blockMs, double latenessMs, bool deadlinePacing) {
    double now = 0.0;
    double due = 0.0;
    for (int i = 0; i < blocks; ++i) {
        if (deadlinePacing) {
            const auto pace = rta::measure::nextSyntheticPace(due, now, blockMs, 500.0);
            due = pace.dueMs;
            if (pace.waitMs > 0) now += pace.waitMs + latenessMs;  // a sleep returns late, never early
        } else {
            now += static_cast<int>(blockMs) + latenessMs;  // the former wait(waitMs_)
            due += blockMs;
        }
    }
    return { now, due };
}
}  // namespace

TEST_CASE("synthetic pacing: a late sleep is repaid, the feed keeps real time", "[synthetic_loopback]") {
    constexpr double kBlockMs = 1000.0 * 512.0 / 48000.0;  // 10.667 ms
    constexpr int kBlocks = 4500;                          // 48 s of audio
    constexpr double kLateness = 5.0;                      // ms, less than one block
    const auto before = simulatePacing(kBlocks, kBlockMs, kLateness, false);
    const auto after = simulatePacing(kBlocks, kBlockMs, kLateness, true);
    // Closed form: the old loop spends floor(blockMs) + lateness = 15 ms per 10.667 ms of audio.
    CHECK(before.dueMs / before.nowMs == Catch::Approx(kBlockMs / (10.0 + kLateness)).epsilon(1e-9));
    CHECK(before.dueMs / before.nowMs < 0.75);
    // The deadline loop ends within one sleep of the audio clock: rate 1.0x.
    CHECK(after.dueMs == Catch::Approx(kBlocks * kBlockMs).epsilon(1e-9));
    CHECK(std::fabs(after.nowMs - after.dueMs) <= 10.0 + kLateness);
}

// L1 (PR #90 verify): back-pressure holds the feed off the bus while a ring is full. A resync
// gate raised in that state must still be acknowledged, or the consumer drains on its timeout
// and the operator reads "CAPTURE RESYNC UNACKNOWLEDGED". The ack timeout is 10 s here, so an
// acknowledgement that only came from the feed's 3 s back-pressure give-up fails the 1.5 s wait.
TEST_CASE("synthetic feed held by a full ring still acknowledges a raised resync gate",
          "[synthetic_loopback]") {
    CaptureBus bus(4096);
    assignRoles(bus);
    SyntheticInput::Config config;
    auto input = std::make_unique<SyntheticInput>(bus, config);
    REQUIRE(waitUntil([&] { return bus.ring(kMeasurementCh)->availableToWrite() < kBlock; }, 5000));

    bus.attachConsumer();
    bus.setResyncAckTimeout(std::chrono::milliseconds(10000));
    REQUIRE(bus.config().setRole(2, ChannelRole::Measurement));  // a new writer: the consumer raises the gate
    bool serviced = false;
    const auto deadline = juce::Time::getMillisecondCounter() + 1500u;
    while (!serviced && juce::Time::getMillisecondCounter() < deadline) {
        serviced = bus.serviceResyncAsConsumer();
        if (!serviced) juce::Thread::sleep(2);
    }
    input.reset();
    CHECK(serviced);
    CHECK(bus.resyncAckTimeouts() == 0);
}

TEST_CASE("synthetic pacing: behind by a block pushes at once, a long stall re-anchors", "[synthetic_loopback]") {
    constexpr double kBlockMs = 10.0;
    // Three blocks behind: due 10, now 40 -> next due 20, already past: no wait, deadline kept.
    const auto behind = rta::measure::nextSyntheticPace(10.0, 40.0, kBlockMs, 500.0);
    CHECK(behind.waitMs == 0);
    CHECK(behind.dueMs == 20.0);
    // A 2 s descheduling: no burst of 200 blocks, the deadline restarts from now.
    const auto stalled = rta::measure::nextSyntheticPace(10.0, 2010.0, kBlockMs, 500.0);
    CHECK(stalled.dueMs == 2010.0);
    CHECK(stalled.waitMs == 0);
    // Ahead of the deadline: sleep the whole milliseconds that remain, never more.
    const auto ahead = rta::measure::nextSyntheticPace(0.0, 2.5, kBlockMs, 500.0);
    CHECK(ahead.dueMs == 10.0);
    CHECK(ahead.waitMs == 7);
}
