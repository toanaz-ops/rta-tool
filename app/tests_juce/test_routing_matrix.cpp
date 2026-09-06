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
