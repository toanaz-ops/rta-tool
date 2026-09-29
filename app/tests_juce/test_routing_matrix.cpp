// SPDX-License-Identifier: AGPL-3.0-or-later
//
// RoutingMatrix composes az_ui's generic GridPanel with the vocabulary
// GridPanel itself must never carry (project CLAUDE.md's module-boundary
// rule). Drives GridPanel::onCellClicked directly, the same trade
// TransferSourceToggle's clickMtwButton()/clickFixedButton() make: a real
// click fires the identical production callback, just without pumping a
// message loop no headless test target runs.

#include <catch2/catch_test_macros.hpp>

#include "view/RoutingMatrix.h"

#include <vector>

using rta::measure::Membership;
using rta::measure::PositionSummary;
using rta::measure::RoutingPlan;
using rta::measure::TransferRoute;
using rta::platform::ChannelConfig;
using rta::platform::ChannelRole;
using rta::view::RoutingMatrix;

TEST_CASE("Clicking a matrix cell cycles the role and calls setTransferFunction",
          "[routing_matrix]") {
    ChannelConfig config;
    RoutingMatrix matrix(config, 3);
    matrix.setActiveTransferFunction(2);

    REQUIRE(config.role(0) == ChannelRole::Unused);
    REQUIRE(config.transferFunction(0) == 0);

    // Unused -> Measurement: the click both sets the role AND tags the
    // channel with the currently active transfer function.
    matrix.grid().onCellClicked(0, 0);
    CHECK(config.role(0) == ChannelRole::Measurement);
    CHECK(config.transferFunction(0) == 2);

    // Measurement -> Reference: still tagged.
    matrix.grid().onCellClicked(0, 0);
    CHECK(config.role(0) == ChannelRole::Reference);
    CHECK(config.transferFunction(0) == 2);

    // Reference -> Unused: the cycle wraps.
    matrix.grid().onCellClicked(0, 0);
    CHECK(config.role(0) == ChannelRole::Unused);

    // Other channels are untouched by a click on channel 0.
    CHECK(config.role(1) == ChannelRole::Unused);
    CHECK(config.role(2) == ChannelRole::Unused);
}

TEST_CASE("A click outside the channel count is ignored", "[routing_matrix]") {
    ChannelConfig config;
    RoutingMatrix matrix(config, 2);

    matrix.grid().onCellClicked(5, 0);  // no such row
    CHECK(config.role(5) == ChannelRole::Unused);  // untouched, no crash
}

TEST_CASE("refreshFromConfig reflects a role change made elsewhere", "[routing_matrix]") {
    ChannelConfig config;
    RoutingMatrix matrix(config, 1);

    REQUIRE(config.setRole(0, ChannelRole::Reference));
    matrix.refreshFromConfig();
    CHECK(matrix.grid().cellText(0, 0) == "REF");
}

TEST_CASE("updateMembership renders a refused route's row differently from an ordinary member's",
          "[routing_matrix]") {
    // Station-4 fix F3 (record §6): a route naming a different reference is
    // refused by AverageGroup::addMember and must be VISIBLE as such here,
    // not indistinguishable from a route that joined the average, and not
    // indistinguishable from a channel no route names at all.
    ChannelConfig config;
    RoutingMatrix matrix(config, 4);

    RoutingPlan plan;
    plan.routes.push_back(TransferRoute{0, 0, 1});  // measurement channel 1: member
    plan.routes.push_back(TransferRoute{1, 5, 2});  // measurement channel 2: refused

    std::vector<PositionSummary> positions(2);
    positions[0].membership = Membership::Member;
    positions[1].membership = Membership::ExcludedDifferentReference;

    matrix.updateMembership(plan, positions);

    CHECK(matrix.grid().cellText(1, 1) == "AVG");
    CHECK(matrix.grid().cellText(2, 1) == "REF!=");
    // Channels 0 and 3 are named by no route at all -- neither state above.
    CHECK(matrix.grid().cellText(0, 1) == "--");
    CHECK(matrix.grid().cellText(3, 1) == "--");
}

TEST_CASE("updateMembership clears a stale row when the route naming it disappears",
          "[routing_matrix]") {
    ChannelConfig config;
    RoutingMatrix matrix(config, 2);

    RoutingPlan plan;
    plan.routes.push_back(TransferRoute{0, 0, 0});
    std::vector<PositionSummary> positions(1);
    positions[0].membership = Membership::Member;
    matrix.updateMembership(plan, positions);
    REQUIRE(matrix.grid().cellText(0, 1) == "AVG");

    matrix.updateMembership(RoutingPlan{}, {});
    CHECK(matrix.grid().cellText(0, 1) == "--");
}

// ---- Lane H2 (D1, D8): rows follow the device's channel count --------------

namespace {

std::vector<std::string> namesOf(int count) {
    std::vector<std::string> names;
    for (int i = 0; i < count; ++i) names.push_back("In " + std::to_string(i + 1));
    return names;
}

}  // namespace

TEST_CASE("Rows follow the device's channel count, past the old 8-row cap",
          "[routing_matrix]") {
    ChannelConfig config;
    RoutingMatrix matrix(config, 0);
    REQUIRE(matrix.channelCount() == 0);

    matrix.setChannelNames(namesOf(16));
    CHECK(matrix.channelCount() == 16);
    // The last row exists as a real cell -- not merely counted.
    CHECK(matrix.grid().cellText(15, 0) == "UNUSED");
    CHECK(matrix.grid().cellText(15, 1) == "--");
}

TEST_CASE("Rows are capped at kMaxChannels, the size of ChannelConfig's tables",
          "[routing_matrix]") {
    ChannelConfig config;
    RoutingMatrix matrix(config, 0);
    matrix.setChannelNames(namesOf(rta::platform::kMaxChannels + 6));
    CHECK(matrix.channelCount() == rta::platform::kMaxChannels);
}

TEST_CASE("Clicking a high row assigns THAT channel with the active transfer function",
          "[routing_matrix]") {
    // Not the identity mapping: transfer function 3, channel 11 (row 12, 1-based).
    // A route position or tf index used as a channel number (memory
    // an-index-from-one-table-used-in-another) would touch channel 3 instead.
    ChannelConfig config;
    RoutingMatrix matrix(config, 0);
    matrix.setChannelNames(namesOf(16));
    matrix.setActiveTransferFunction(3);

    REQUIRE(config.setRole(0, ChannelRole::Reference));
    REQUIRE(config.setTransferFunction(0, 3));

    matrix.grid().onCellClicked(11, 0);  // Unused -> Measurement

    CHECK(config.role(11) == ChannelRole::Measurement);
    CHECK(config.transferFunction(11) == 3);
    CHECK(config.role(3) == ChannelRole::Unused);
    CHECK(config.transferFunction(3) == 0);

    // And the route the click produced: transfer function 3 measures channel
    // 11 against reference 0, at the channel count the bus would report.
    const RoutingPlan plan = rta::measure::planRouting(config, 16);
    REQUIRE(plan.routes.size() == 1);
    CHECK(plan.routes[0].tfIndex == 3);
    CHECK(plan.routes[0].measurementChannel == 11);
    CHECK(plan.routes[0].referenceChannel == 0);
}

TEST_CASE("The AVG column shows a route on a channel past row 8", "[routing_matrix]") {
    ChannelConfig config;
    RoutingMatrix matrix(config, 0);
    matrix.setChannelNames(namesOf(16));

    RoutingPlan plan;
    plan.routes.push_back(TransferRoute{3, 0, 12});
    std::vector<PositionSummary> positions(1);
    positions[0].membership = Membership::Member;
    matrix.updateMembership(plan, positions);

    CHECK(matrix.grid().cellText(12, 1) == "AVG");
}

TEST_CASE("Shrinking the channel list drops rows and leaves the config alone",
          "[routing_matrix]") {
    // Decision (RoutingMatrix::setChannelNames): a role on a dropped channel is
    // NOT cleared. It cannot route (planRouting clamps to the bus's channel
    // count), it is not silently rewritten behind ChannelRoleTable's back, and
    // it survives a transient device close.
    ChannelConfig config;
    RoutingMatrix matrix(config, 0);
    matrix.setChannelNames(namesOf(16));
    matrix.setActiveTransferFunction(3);
    REQUIRE(config.setRole(0, ChannelRole::Reference));
    REQUIRE(config.setTransferFunction(0, 3));
    matrix.grid().onCellClicked(11, 0);
    REQUIRE(rta::measure::planRouting(config, 16).routes.size() == 1);

    matrix.setChannelNames(namesOf(4));

    CHECK(matrix.channelCount() == 4);
    CHECK(config.role(11) == ChannelRole::Measurement);  // untouched

    // No row 11 any more: a click there is refused, a membership entry naming
    // it is dropped, neither crashes or writes anything.
    matrix.grid().onCellClicked(11, 0);
    CHECK(config.role(11) == ChannelRole::Measurement);
    RoutingPlan stale;
    stale.routes.push_back(TransferRoute{3, 0, 11});
    std::vector<PositionSummary> positions(1);
    positions[0].membership = Membership::Member;
    matrix.updateMembership(stale, positions);
    CHECK(matrix.grid().cellText(11, 1).empty());

    // And the route itself cannot exist at the shrunk channel count.
    const RoutingPlan shrunk = rta::measure::planRouting(config, 4);
    CHECK(shrunk.routes.empty());
    CHECK(shrunk.unroutedMeasurements.empty());
}

TEST_CASE("A row rebuild re-reads the roles from the config", "[routing_matrix]") {
    ChannelConfig config;
    REQUIRE(config.setRole(2, ChannelRole::Reference));
    RoutingMatrix matrix(config, 0);
    matrix.setChannelNames(namesOf(4));
    CHECK(matrix.grid().cellText(2, 0) == "REF");
}

TEST_CASE("Rows scroll inside the widget's own height once they overflow",
          "[routing_matrix]") {
    ChannelConfig config;
    RoutingMatrix matrix(config, 0);
    matrix.setSize(240, 220);  // the rail's slot (MainComponentRail.cpp)

    auto contentHeight = [&] { return matrix.viewportForTest().getViewedComponent()->getHeight(); };

    matrix.setChannelNames(namesOf(16));
    CHECK(matrix.viewportForTest().getHeight() == 220);
    CHECK(contentHeight() > matrix.viewportForTest().getHeight());
    CHECK(contentHeight() == RoutingMatrix::kMinRowHeight * 16 + az::ui::captionHeight);
    CHECK(matrix.scrollBarVisibleForTest());

    matrix.setChannelNames(namesOf(64));
    CHECK(contentHeight() == RoutingMatrix::kMinRowHeight * 64 + az::ui::captionHeight);

    // 8 rows always fit the slot (the pre-H2 shape): no scrolling, no bar.
    matrix.setChannelNames(namesOf(8));
    CHECK(contentHeight() == matrix.viewportForTest().getHeight());
    CHECK_FALSE(matrix.scrollBarVisibleForTest());

    matrix.setChannelNames(namesOf(4));
    CHECK(contentHeight() == matrix.viewportForTest().getHeight());
    CHECK_FALSE(matrix.scrollBarVisibleForTest());
}
