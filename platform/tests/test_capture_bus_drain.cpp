// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_platform_types tests. CaptureBus::drainAsConsumer, the
// consumer-side drain that replaced the ring reset prepare() used to do (audit
// prj010-fable-audit-20261021, high). prepare() leaves the rings alone; these
// cases pin what the consumer's drain must and must not do while the producer
// may be live.
#include "rta/platform/CaptureBus.h"

#include <catch2/catch_test_macros.hpp>

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

}  // namespace

TEST_CASE("drainAsConsumer keeps writer rings aligned while a block is in flight",
          "[capturebus]") {
    // The producer writes a block channel by channel, so a drain can land
    // between channel 0 and channel 1 of block k. Draining "everything
    // available" per ring then leaves channel 1 holding block k that channel
    // 0 no longer has: a permanent one-block offset between the pair.
    CaptureBus bus(4096);
    bus.prepare(48000.0, 2);
    REQUIRE(bus.config().setRole(0, ChannelRole::Reference));
    REQUIRE(bus.config().setRole(1, ChannelRole::Measurement));
    bus.setActive(true);

    Block block(2, 512, 1.0f);
    bus.pushFromCallback(block.pointers.data(), 2, 512);
    // Block k reaches channel 0 only: the producer is between its two writes.
    const std::vector<float> inFlight(64, 7.0f);
    REQUIRE(bus.ring(0)->write(inFlight));
    REQUIRE(bus.ring(0)->availableToRead() == 576);
    REQUIRE(bus.ring(1)->availableToRead() == 512);

    bus.drainAsConsumer();

    // The producer finishes block k.
    REQUIRE(bus.ring(1)->write(inFlight));
    CHECK(bus.ring(0)->availableToRead() == 64);
    CHECK(bus.ring(1)->availableToRead() == 64);
}

TEST_CASE("drainAsConsumer empties a ring that has no writer completely", "[capturebus]") {
    CaptureBus bus(4096);
    bus.prepare(48000.0, 2);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));  // channel 1 stays Unused
    bus.setActive(true);

    const std::vector<float> stale(100, 3.0f);
    REQUIRE(bus.ring(1)->write(stale));
    REQUIRE(bus.ring(0)->write(stale));
    bus.drainAsConsumer();

    CHECK(bus.ring(0)->availableToRead() == 0);
    CHECK(bus.ring(1)->availableToRead() == 0);
}
