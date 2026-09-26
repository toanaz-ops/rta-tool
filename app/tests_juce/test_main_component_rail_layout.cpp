// SPDX-License-Identifier: AGPL-3.0-or-later
// Fix round PR #40, MEDIUM F1: item 4's rail-scroll fix
// (MainComponentRail.cpp's own `layout()`) had no test. The mutant that
// turns `juce::jmax(bounds.getHeight(), railContentHeight)` into
// `bounds.getHeight()` survives the whole suite unchanged (1114/1114) --
// nothing anywhere asserted the widgets' NATURAL size, only that
// construction and resize did not crash -- and the render then reproduces
// the exact defect item 4 fixed: routingMatrix_ squeezed to a sliver (its
// GridPanel has no minimum row height) and channelRoleTable_ clipped to
// zero pixels.
//
// 1280x800 is deliberate, not an arbitrary size: it is the exact window
// tools/snapshot.cpp renders main-live.png/main-live-spl.png at, and the
// size that first exposed the bug (PR #40's own before/after screenshots).
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"

TEST_CASE("rail: routingMatrix_ keeps its full natural height at 1280x800",
         "[main_component_rail]") {
    MainComponent component;
    component.setSize(1280, 800);
    // setSize() alone does not call resized() on a component with no
    // desktop peer (tools/snapshot.cpp's own trap-T6 comment) -- explicit,
    // same as every other headless construction in this tree.
    component.resized();

    // kRoutingMatrixHeight (MainComponentRail.cpp): 8 channel rows plus one
    // header row, sized to be comfortably readable -- see that constant's
    // own comment. The mutant this test exists to catch clamps the widget
    // to whatever the rail's raw, unscrolled height happens to be instead.
    constexpr int kExpectedRoutingMatrixHeight = 220;
    CHECK(MainComponentTestAccess::routingMatrix(component).getHeight() == kExpectedRoutingMatrixHeight);
}

TEST_CASE("rail: channelRoleTable_ keeps at least its scroll-content minimum height at 1280x800",
         "[main_component_rail]") {
    MainComponent component;
    component.setSize(1280, 800);
    component.resized();

    // kChannelRoleTableMinHeight (MainComponentRail.cpp): header + ~4 rows.
    // The pre-fix layout gave this exactly ZERO pixels once
    // devicePanel_ + routingMatrix_ exhausted the rail's own height --
    // `>= ` rather than `==` because a taller window is free to give it more.
    constexpr int kMinChannelRoleTableHeight = 150;
    CHECK(MainComponentTestAccess::channelRoleTable(component).getHeight() >= kMinChannelRoleTableHeight);
}
