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

TEST_CASE("rail: the scrollbar's width is not reserved when the rail's content already fits",
         "[main_component_rail]") {
    // Lane-end LOW batch, 2026-09-27 (R2-1): MainComponentRail.cpp's own
    // `layout()` only reserves `railScrollView_.getScrollBarThickness()`
    // from `contentWidth` when `contentOverflows` (fix round LOW F4). Every
    // test above is at 1280x800, where the rail's own natural content
    // height already exceeds 800 and `contentOverflows` is always TRUE --
    // the `: 0` branch has never run under test. A window tall enough that
    // devicePanel_ + 2 gaps + kRoutingMatrixHeight(220) + 2 gaps +
    // kChannelRoleTableMinHeight(150) already fits (well under 3000px)
    // takes that branch instead.
    MainComponent tall;
    tall.setSize(1280, 3000);
    tall.resized();
    MainComponent overflowing;
    overflowing.setSize(1280, 800);
    overflowing.resized();

    // The mutant this test exists to catch: always subtracting the
    // scrollbar thickness (dropping the `contentOverflows ?` altogether)
    // leaves both windows narrowed by the SAME fixed amount, so the two
    // widths compare equal instead of the tall window's being wider by
    // exactly one scrollbar. `>` alone (not a specific pixel count) is the
    // whole claim: kRailWidth (MainComponentLayout.cpp) is private to that
    // file, so this does not also pin its value.
    CHECK(MainComponentTestAccess::routingMatrix(tall).getWidth() >
         MainComponentTestAccess::routingMatrix(overflowing).getWidth());
    // Both widgets share one `railScrollContent_` width (MainComponentRail.
    // cpp's own `layout()`: `content = railScrollContent_.getLocalBounds()`,
    // never narrowed per-child) -- channelRoleTable_ must show the SAME
    // width difference, not just routingMatrix_.
    CHECK(MainComponentTestAccess::channelRoleTable(tall).getWidth() ==
         MainComponentTestAccess::routingMatrix(tall).getWidth());
}

// ---- Lane H2 (D1, D8) ------------------------------------------------------

namespace {

std::vector<std::string> inputNames(int count) {
    std::vector<std::string> names;
    for (int i = 0; i < count; ++i) names.push_back("In " + std::to_string(i + 1));
    return names;
}

}  // namespace

TEST_CASE("rail: the outer viewport is parented, placed, and scrolls the content (D8)",
         "[main_component_rail]") {
    // Until now nothing pinned railScrollView_'s own setBounds/addAndMakeVisible
    // (only the three content widgets' sizes), so deleting either left the
    // whole suite green while the rail vanished.
    MainComponent component;
    component.setSize(1280, 800);
    component.resized();

    const juce::Viewport& viewport = MainComponentTestAccess::railViewport(component);
    CHECK(viewport.getParentComponent() == &component);
    CHECK(viewport.isVisible());
    CHECK(viewport.getViewedComponent() == &MainComponentTestAccess::railContent(component));

    // MainComponent::resized(): the rail column starts at the outer margin and
    // its last row runs to the bottom margin (2 gaps each) -- closed form
    // from that function, no private width constant needed.
    CHECK(viewport.getX() == az::ui::gap * 2);
    CHECK(viewport.getBottom() == component.getHeight() - az::ui::gap * 2);
    CHECK(viewport.getWidth() > 0);

    // At 800 px the stacked widgets do not fit, so the content is taller than
    // what the viewport shows (that is what makes it scroll).
    CHECK(MainComponentTestAccess::railContent(component).getHeight() > viewport.getHeight());
}

TEST_CASE("rail: 16 input channels scroll inside the 220 px slot and overlap nothing (D1)",
         "[main_component_rail]") {
    MainComponent component;
    component.setSize(1280, 800);
    MainComponentTestAccess::applyChannelNamesForTest(component, inputNames(16));
    component.resized();

    const juce::Component& panel = MainComponentTestAccess::devicePanel(component);
    const juce::Component& matrix = MainComponentTestAccess::routingMatrix(component);
    const juce::Component& roles = MainComponentTestAccess::channelRoleTable(component);
    const juce::Viewport& matrixViewport = MainComponentTestAccess::routingMatrixWidget(component).viewportForTest();

    // The slot did not grow with the channel count...
    CHECK(matrix.getHeight() == 220);
    // ...so the rows after it stay where they were: no widget overlaps the next.
    CHECK(panel.getBottom() <= matrix.getY());
    CHECK(matrix.getBottom() <= roles.getY());
    // ...and 16 rows (header + 16 * kMinRowHeight) really are taller than the
    // slot, i.e. they scroll rather than being squeezed.
    CHECK(matrixViewport.getViewedComponent()->getHeight() > matrixViewport.getHeight());
    CHECK(MainComponentTestAccess::routingMatrixWidget(component).channelCount() == 16);
}

TEST_CASE("rail: 4 input channels show no scrollbar in the routing matrix (D1)",
         "[main_component_rail]") {
    MainComponent component;
    component.setSize(1280, 800);
    MainComponentTestAccess::applyChannelNamesForTest(component, inputNames(4));
    component.resized();

    const juce::Viewport& matrixViewport = MainComponentTestAccess::routingMatrixWidget(component).viewportForTest();
    CHECK(MainComponentTestAccess::routingMatrixWidget(component).channelCount() == 4);
    CHECK(matrixViewport.getViewedComponent()->getHeight() <= matrixViewport.getHeight());
    CHECK_FALSE(MainComponentTestAccess::routingMatrixWidget(component).scrollBarVisibleForTest());
}

TEST_CASE("rail: the membership plan uses the bus's channel count, not the TF cap (D1)",
         "[main_component_rail]") {
    // refreshMembershipFromSnapshot used to plan with kMaxTransferFunctions (8)
    // as the channel count, so on a 16-input device a measurement on channel 11
    // dropped out of the plan the analysis thread still routed -- and
    // updateMembership zips that plan against Snapshot::positions by index.
    MainComponent component;
    MainComponentTestAccess::prepareBusForTest(component, 48000.0, 16);
    auto& config = MainComponentTestAccess::channelConfigForTest(component);
    using rta::platform::ChannelRole;
    REQUIRE(config.setRole(0, ChannelRole::Reference));
    REQUIRE(config.setTransferFunction(0, 3));
    REQUIRE(config.setRole(11, ChannelRole::Measurement));
    REQUIRE(config.setTransferFunction(11, 3));

    const auto plan = MainComponentTestAccess::currentRoutingPlanForTest(component);
    REQUIRE(plan.routes.size() == 1);
    CHECK(plan.routes[0].measurementChannel == 11);
    CHECK(plan.routes[0].tfIndex == 3);
}
