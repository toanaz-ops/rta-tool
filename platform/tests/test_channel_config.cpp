// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "rta/platform/ChannelConfig.h"

using rta::platform::ChannelConfig;
using rta::platform::ChannelRole;
using rta::platform::kMaxChannels;

TEST_CASE("Channels default to Unused", "[channelconfig]") {
    ChannelConfig config;
    for (int ch = 0; ch < kMaxChannels; ++ch) {
        CHECK(config.role(ch) == ChannelRole::Unused);
    }
}

TEST_CASE("A role outside kMaxChannels is refused", "[channelconfig]") {
    ChannelConfig config;
    CHECK_FALSE(config.setRole(kMaxChannels, ChannelRole::Measurement));
    CHECK_FALSE(config.setRole(-1, ChannelRole::Measurement));
    // No crash, and nothing in range was touched by the refused calls.
    for (int ch = 0; ch < kMaxChannels; ++ch) {
        CHECK(config.role(ch) == ChannelRole::Unused);
    }
}

TEST_CASE("A role beyond the callback's channel count is ignored", "[channelconfig]") {
    // This is the record's bounds rule: clip to what the callback received,
    // never to what the device advertised.
    ChannelConfig config;
    REQUIRE(config.setRole(5, ChannelRole::Measurement));

    std::array<ChannelRole, static_cast<std::size_t>(kMaxChannels)> out{};
    config.snapshot(2, out);

    CHECK(out[5] == ChannelRole::Unused);
    CHECK(out[0] == ChannelRole::Unused);
    CHECK(out[1] == ChannelRole::Unused);
}

TEST_CASE("A role within the received channel count survives the snapshot", "[channelconfig]") {
    ChannelConfig config;
    REQUIRE(config.setRole(1, ChannelRole::Reference));

    std::array<ChannelRole, static_cast<std::size_t>(kMaxChannels)> out{};
    config.snapshot(2, out);

    CHECK(out[0] == ChannelRole::Unused);
    CHECK(out[1] == ChannelRole::Reference);
}

TEST_CASE("firstChannelWithRole finds the lowest match", "[channelconfig]") {
    ChannelConfig config;
    REQUIRE(config.setRole(7, ChannelRole::Reference));
    REQUIRE(config.setRole(3, ChannelRole::Reference));

    CHECK(config.firstChannelWithRole(ChannelRole::Reference) == 3);
    CHECK(config.firstChannelWithRole(ChannelRole::Measurement) == -1);
}

TEST_CASE("configEpoch increments on every accepted role change, never on a refused one",
          "[channelconfig]") {
    ChannelConfig config;
    const auto start = config.configEpoch();

    REQUIRE(config.setRole(0, ChannelRole::Measurement));
    CHECK(config.configEpoch() == start + 1);

    CHECK_FALSE(config.setRole(kMaxChannels, ChannelRole::Measurement));
    CHECK(config.configEpoch() == start + 1);  // refused: no bump

    REQUIRE(config.setRole(0, ChannelRole::Unused));
    CHECK(config.configEpoch() == start + 2);
}

// --- B1: the routing table (record §6) -------------------------------------

TEST_CASE("channelsWithRole returns every channel of a role in ascending index order",
          "[channelconfig]") {
    ChannelConfig config;
    REQUIRE(config.setRole(7, ChannelRole::Measurement));
    REQUIRE(config.setRole(2, ChannelRole::Measurement));
    REQUIRE(config.setRole(5, ChannelRole::Measurement));
    REQUIRE(config.setRole(9, ChannelRole::Reference));  // a different role: must not appear

    std::array<int, 8> out{};
    const int count = config.channelsWithRole(ChannelRole::Measurement, out);
    REQUIRE(count == 3);
    CHECK(out[0] == 2);
    CHECK(out[1] == 5);
    CHECK(out[2] == 7);
    // Agrees with firstChannelWithRole on the first element (same lowest
    // match, two different lookups).
    CHECK(config.firstChannelWithRole(ChannelRole::Measurement) == out[0]);
}

TEST_CASE("channelsWithRole is empty for a role nothing holds", "[channelconfig]") {
    ChannelConfig config;
    REQUIRE(config.setRole(0, ChannelRole::Measurement));

    std::array<int, 4> out{};
    CHECK(config.channelsWithRole(ChannelRole::Reference, out) == 0);
}

TEST_CASE("channelsWithRole clamps to the output span's capacity", "[channelconfig]") {
    ChannelConfig config;
    REQUIRE(config.setRole(0, ChannelRole::Measurement));
    REQUIRE(config.setRole(1, ChannelRole::Measurement));
    REQUIRE(config.setRole(2, ChannelRole::Measurement));

    std::array<int, 2> out{};
    // Three channels hold the role; the span only has room for two.
    CHECK(config.channelsWithRole(ChannelRole::Measurement, out) == 2);
    CHECK(out[0] == 0);
    CHECK(out[1] == 1);
}

TEST_CASE("A transfer-function index defaults to 0 and round-trips", "[channelconfig]") {
    ChannelConfig config;
    CHECK(config.transferFunction(3) == 0);
    REQUIRE(config.setTransferFunction(3, 2));
    CHECK(config.transferFunction(3) == 2);
    // Untouched channels stay at the default.
    CHECK(config.transferFunction(0) == 0);
}

TEST_CASE("setTransferFunction is refused outside [0, kMaxChannels) on either argument",
          "[channelconfig]") {
    ChannelConfig config;
    CHECK_FALSE(config.setTransferFunction(-1, 0));
    CHECK_FALSE(config.setTransferFunction(kMaxChannels, 0));
    CHECK_FALSE(config.setTransferFunction(0, -1));
    CHECK_FALSE(config.setTransferFunction(0, kMaxChannels));
    // No refused call left a mark.
    CHECK(config.transferFunction(0) == 0);
}

TEST_CASE("Every successful setTransferFunction bumps configEpoch, a refused one never does",
          "[channelconfig]") {
    ChannelConfig config;
    const auto start = config.configEpoch();

    REQUIRE(config.setTransferFunction(0, 1));
    CHECK(config.configEpoch() == start + 1);

    CHECK_FALSE(config.setTransferFunction(-1, 1));
    CHECK(config.configEpoch() == start + 1);  // refused: no bump

    REQUIRE(config.setTransferFunction(0, 2));
    CHECK(config.configEpoch() == start + 2);
}
