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
