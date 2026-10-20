// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_platform_types tests. The resync that replaced the
// ring reset prepare() used to do (audit prj010-fable-audit-20261021, high; PR
// #78 fix round 1, MEDIUM). prepare() leaves the rings alone and raises a
// producer gate; the consumer's serviceResyncAsConsumer() empties every ring
// behind the gate and only then lowers it, so every ring restarts empty on one
// callback boundary. These tests play the consumer by calling it directly.
#include "rta/platform/CaptureBus.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>
#include <vector>

using rta::platform::CaptureBus;
using rta::platform::ChannelRole;

namespace {

/// `numChannels` x `numSamples` block of `fill`, as the callback's
/// `const float* const*`.
struct Block {
    std::vector<std::vector<float>> channels;
    std::vector<const float*> pointers;
    Block(int numChannels, int numSamples, float fill) {
        channels.assign(static_cast<std::size_t>(numChannels),
                        std::vector<float>(static_cast<std::size_t>(numSamples), fill));
        for (auto& ch : channels) pointers.push_back(ch.data());
    }
};

void push(CaptureBus& bus, int channels, int samples) {
    Block block(channels, samples, 1.0f);
    bus.pushFromCallback(block.pointers.data(), channels, samples);
}

std::size_t avail(CaptureBus& bus, int ch) { return bus.ring(ch)->availableToRead(); }

}  // namespace

TEST_CASE("while the resync gate is up the producer writes no ring and counts resync frames, not drops",
          "[capturebus][resync]") {
    CaptureBus bus(4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Reference));
    REQUIRE(bus.config().setRole(1, ChannelRole::Measurement));
    bus.attachConsumer();
    bus.prepare(48000.0, 2);
    bus.setActive(true);
    REQUIRE(bus.resyncPending());

    push(bus, 2, 256);
    CHECK(avail(bus, 0) == 0);
    CHECK(avail(bus, 1) == 0);
    CHECK(bus.resyncFrames() == 256);
    CHECK(bus.resyncLostFrames() == 256);
    CHECK(bus.totalDrops() == 0);

    REQUIRE(bus.serviceResyncAsConsumer());
    CHECK_FALSE(bus.resyncPending());
    push(bus, 2, 128);
    CHECK(avail(bus, 0) == 128);
    CHECK(avail(bus, 1) == 128);
}

TEST_CASE("with no consumer attached prepare raises no gate", "[capturebus][resync]") {
    CaptureBus bus(4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    bus.prepare(48000.0, 1);
    bus.setActive(true);
    CHECK_FALSE(bus.resyncPending());
    push(bus, 1, 64);
    CHECK(avail(bus, 0) == 64);
    CHECK(bus.resyncFrames() == 0);
}

TEST_CASE("a role assigned while running, then an epoch change, leaves every writer ring empty and aligned",
          "[capturebus][resync]") {
    // The verifier's instance: ch0 Ref and ch1 Meas run and the consumer reads a
    // hop; the operator then assigns ch2 Meas (no epoch); more blocks land; the
    // device is re-prepared. Draining an equal count (or each ring's own
    // backlog) left ch0/ch1 holding old-rate samples that ch2 never had: a
    // permanent 512-sample offset between ref and ch2.
    CaptureBus bus(8192);
    REQUIRE(bus.config().setRole(0, ChannelRole::Reference));
    REQUIRE(bus.config().setRole(1, ChannelRole::Measurement));
    bus.prepare(48000.0, 3);
    bus.attachConsumer();
    bus.setActive(true);

    push(bus, 3, 512);
    REQUIRE(bus.ring(0)->discard(512) == 512);  // the consumer drains a hop...
    REQUIRE(bus.ring(1)->discard(512) == 512);  // ...from both rings it reads
    REQUIRE(bus.config().setRole(2, ChannelRole::Measurement));  // no epoch
    CHECK_FALSE(bus.serviceResyncAsConsumer());  // tick 1: sees the new writer, raises the gate
    CHECK(bus.resyncPending());
    // (the gate is up, so the next two blocks land nowhere; before the fix they
    // landed on ch0..ch2 and the stale backlog was never reconciled)
    push(bus, 3, 512);
    push(bus, 3, 512);

    bus.setActive(false);  // a device change stops the device before prepare()...
    bus.prepare(96000.0, 3);
    REQUIRE(bus.serviceResyncAsConsumer());  // drains every ring, then lowers the gate
    bus.setActive(true);                     // ...and starts it after

    CHECK(avail(bus, 0) == 0);
    CHECK(avail(bus, 1) == 0);
    CHECK(avail(bus, 2) == 0);
    push(bus, 3, 256);  // the next common block
    CHECK(avail(bus, 0) == 256);
    CHECK(avail(bus, 1) == 256);
    CHECK(avail(bus, 2) == 256);
}

TEST_CASE("a role change alone realigns the writer rings", "[capturebus][resync]") {
    CaptureBus bus(8192);
    REQUIRE(bus.config().setRole(0, ChannelRole::Reference));
    REQUIRE(bus.config().setRole(1, ChannelRole::Measurement));
    bus.prepare(48000.0, 3);
    bus.attachConsumer();
    bus.setActive(true);

    push(bus, 3, 512);  // ch0 and ch1 now hold a backlog ch2 will never have
    REQUIRE(avail(bus, 0) == 512);
    REQUIRE(bus.config().setRole(2, ChannelRole::Measurement));
    const auto sequenceBefore = bus.resyncSequence();
    // Tick 1 raises the gate, but no callback has SEEN it yet, so one might
    // still be mid-block: the consumer must not drain.
    CHECK_FALSE(bus.serviceResyncAsConsumer());
    CHECK(bus.resyncPending());
    CHECK(avail(bus, 0) == 512);
    push(bus, 3, 64);  // a callback observes the gate up: the acknowledgement
    CHECK(bus.serviceResyncAsConsumer());
    CHECK(avail(bus, 0) == 0);
    CHECK(bus.resyncSequence() != sequenceBefore);
    // Everything thrown away is reported: the 64 frames the gate refused and
    // the 512-sample backlog the drain discarded (the SPL feed adds this to
    // the channel's drop count, so the block carries Gap).
    CHECK(bus.resyncLostFrames() == 64 + 512);

    push(bus, 3, 256);
    CHECK(avail(bus, 0) == 256);
    CHECK(avail(bus, 1) == 256);
    CHECK(avail(bus, 2) == 256);
}

TEST_CASE("a transfer-function change does not resync: the written set is the same",
          "[capturebus][resync]") {
    CaptureBus bus(4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Reference));
    REQUIRE(bus.config().setRole(1, ChannelRole::Measurement));
    bus.prepare(48000.0, 2);
    bus.attachConsumer();
    bus.setActive(true);
    push(bus, 2, 128);
    REQUIRE(bus.config().setTransferFunction(1, 3));
    CHECK_FALSE(bus.serviceResyncAsConsumer());
    CHECK_FALSE(bus.resyncPending());
    CHECK(avail(bus, 0) == 128);
}

TEST_CASE("detaching the consumer lowers a raised gate", "[capturebus][resync]") {
    CaptureBus bus(4096);
    bus.attachConsumer();
    bus.prepare(48000.0, 1);
    REQUIRE(bus.resyncPending());
    bus.detachConsumer();
    CHECK_FALSE(bus.resyncPending());
}

TEST_CASE("drainAsConsumer empties rings even when their backlogs differ", "[capturebus][resync]") {
    // Draining an equal count (the smallest availability) left the longer
    // ring holding old-rate samples at its front -- the very offset the
    // resync exists to remove.
    CaptureBus bus(4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Reference));
    REQUIRE(bus.config().setRole(2, ChannelRole::Measurement));
    bus.prepare(48000.0, 3);
    bus.setActive(true);
    const std::vector<float> backlog(300, 2.0f);
    REQUIRE(bus.ring(0)->write(backlog));  // ch2 never had these
    bus.drainAsConsumer();
    CHECK(avail(bus, 0) == 0);
    CHECK(avail(bus, 2) == 0);
}

TEST_CASE("an inactive bus is drained at once: no callback can be in flight", "[capturebus][resync]") {
    CaptureBus bus(4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    bus.attachConsumer();
    bus.prepare(48000.0, 1);
    bus.setActive(false);
    const std::vector<float> stale(64, 1.0f);
    REQUIRE(bus.ring(0)->write(stale));
    CHECK(bus.serviceResyncAsConsumer());
    CHECK(avail(bus, 0) == 0);
}

TEST_CASE("with no acknowledgement the drain goes ahead after the bounded wait, and says so",
          "[capturebus][resync]") {
    CaptureBus bus(4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    bus.attachConsumer();
    bus.setResyncAckTimeout(std::chrono::milliseconds(20));
    bus.prepare(48000.0, 1);
    bus.setActive(true);  // active, but no callback ever arrives
    CHECK_FALSE(bus.serviceResyncAsConsumer());
    CHECK(bus.resyncAckTimeouts() == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    CHECK(bus.serviceResyncAsConsumer());
    CHECK(bus.resyncAckTimeouts() == 1);
}
