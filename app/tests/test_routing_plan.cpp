// SPDX-License-Identifier: AGPL-3.0-or-later
//
// planRouting() is a pure function over ChannelConfig precisely so the
// routing decision (record §6: N transfer functions, one drain, each
// reference read once) is testable with RTA_BUILD_APP=OFF -- the same
// reason PairedDrain.h's pairedHopCount is a free function rather than a
// private AnalysisThread method.

#include <catch2/catch_test_macros.hpp>

#include "measure/RoutingPlan.h"

using rta::measure::planRouting;
using rta::measure::RoutingPlan;
using rta::platform::ChannelConfig;
using rta::platform::ChannelRole;

TEST_CASE("Two measurements sharing one reference give one distinct reference, two routes",
          "[routingplan]") {
    ChannelConfig config;
    REQUIRE(config.setRole(0, ChannelRole::Reference));
    REQUIRE(config.setRole(1, ChannelRole::Measurement));
    REQUIRE(config.setRole(2, ChannelRole::Measurement));
    // Default tfIndex is 0 for every channel -- channel 0 (Reference) and
    // channels 1, 2 (Measurement) all name tf 0 without touching
    // setTransferFunction at all.

    const RoutingPlan plan = planRouting(config, 3);
    REQUIRE(plan.routes.size() == 2);
    CHECK(plan.routes[0].measurementChannel == 1);
    CHECK(plan.routes[0].referenceChannel == 0);
    CHECK(plan.routes[0].tfIndex == 0);
    CHECK(plan.routes[1].measurementChannel == 2);
    CHECK(plan.routes[1].referenceChannel == 0);
    REQUIRE(plan.distinctReferences.size() == 1);
    CHECK(plan.distinctReferences[0] == 0);
    CHECK(plan.unroutedMeasurements.empty());
}

TEST_CASE("Two measurements naming different references give two distinct references",
          "[routingplan]") {
    ChannelConfig config;
    REQUIRE(config.setRole(0, ChannelRole::Reference));
    REQUIRE(config.setRole(1, ChannelRole::Reference));
    REQUIRE(config.setTransferFunction(1, 1));  // channel 1 names tf 1
    REQUIRE(config.setRole(2, ChannelRole::Measurement));
    REQUIRE(config.setTransferFunction(2, 0));  // tf 0 -> reference channel 0
    REQUIRE(config.setRole(3, ChannelRole::Measurement));
    REQUIRE(config.setTransferFunction(3, 1));  // tf 1 -> reference channel 1

    const RoutingPlan plan = planRouting(config, 4);
    REQUIRE(plan.routes.size() == 2);
    CHECK(plan.routes[0].measurementChannel == 2);
    CHECK(plan.routes[0].referenceChannel == 0);
    CHECK(plan.routes[1].measurementChannel == 3);
    CHECK(plan.routes[1].referenceChannel == 1);
    REQUIRE(plan.distinctReferences.size() == 2);
    CHECK(plan.distinctReferences[0] == 0);
    CHECK(plan.distinctReferences[1] == 1);
}

TEST_CASE("A measurement whose TF names no reference lands in unroutedMeasurements",
          "[routingplan]") {
    ChannelConfig config;
    REQUIRE(config.setRole(0, ChannelRole::Reference));
    // Channel 0's tf is 0 (default).
    REQUIRE(config.setRole(1, ChannelRole::Measurement));
    REQUIRE(config.setTransferFunction(1, 5));  // no channel names tf 5 as Reference

    const RoutingPlan plan = planRouting(config, 2);
    CHECK(plan.routes.empty());
    REQUIRE(plan.unroutedMeasurements.size() == 1);
    CHECK(plan.unroutedMeasurements[0] == 1);
    // Never silently paired with channel 0, the ungated default the old
    // firstChannelWithRole-only drain would have used.
    CHECK(plan.distinctReferences.empty());
}

TEST_CASE("planRouting clamps to channelCount and ignores channels beyond it",
          "[routingplan]") {
    ChannelConfig config;
    REQUIRE(config.setRole(0, ChannelRole::Reference));
    REQUIRE(config.setRole(1, ChannelRole::Measurement));
    // channelCount = 1: channel 1's Measurement role is invisible to this call.
    const RoutingPlan plan = planRouting(config, 1);
    CHECK(plan.routes.empty());
    CHECK(plan.unroutedMeasurements.empty());
}

TEST_CASE("Routes are ordered ascending by measurement channel regardless of setup order",
          "[routingplan]") {
    ChannelConfig config;
    REQUIRE(config.setRole(0, ChannelRole::Reference));
    REQUIRE(config.setRole(5, ChannelRole::Measurement));
    REQUIRE(config.setRole(2, ChannelRole::Measurement));

    const RoutingPlan plan = planRouting(config, 6);
    REQUIRE(plan.routes.size() == 2);
    CHECK(plan.routes[0].measurementChannel == 2);
    CHECK(plan.routes[1].measurementChannel == 5);
}
