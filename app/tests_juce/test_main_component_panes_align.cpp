// SPDX-License-Identifier: AGPL-3.0-or-later
// ALIGN-UI lane wave A, task T5: the sixth selector button reaches the ALIGN
// pane through the real MainComponent and the real makePaneFactory, and
// leaving the pane and coming back builds a new one over the SAME runner (the
// model lives in MainComponentAlign, not in the pane).
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "view/AlignPaneView.h"

namespace {
using Access = MainComponentTestAccess;
constexpr const char* kTag = "[main_component_panes_align]";

rta::view::AlignPaneView* alignPaneOf(MainComponent& c) {
    return dynamic_cast<rta::view::AlignPaneView*>(const_cast<juce::Component*>(&Access::pane(c)));
}
}  // namespace

TEST_CASE("align pane: the selector builds an AlignPaneView and returns to RTA", kTag) {
    MainComponent c;
    CHECK(c.currentPaneView() == rta::view::PaneView::Rta);
    CHECK(alignPaneOf(c) == nullptr);

    c.selectPaneView(rta::view::PaneSelectorButton::Align);
    CHECK(c.currentPaneView() == rta::view::PaneView::Align);
    REQUIRE(alignPaneOf(c) != nullptr);

    c.selectPaneView(rta::view::PaneSelectorButton::Rta);
    CHECK(c.currentPaneView() == rta::view::PaneView::Rta);
    CHECK(alignPaneOf(c) == nullptr);
}

TEST_CASE("align pane: answers written through the combos reach the runner and outlive the pane", kTag) {
    MainComponent c;
    c.selectPaneView(rta::view::PaneSelectorButton::Align);
    auto* pane = alignPaneOf(c);
    REQUIRE(pane != nullptr);
    pane->topologyComboForTest().setSelectedId(2, juce::sendNotificationSync);
    pane->highPassComboForTest().setSelectedId(1, juce::sendNotificationSync);
    const auto& answers = Access::alignForTest(c).runnerForTest().answers();
    CHECK(answers.topology.has_value());
    CHECK(answers.highPass.has_value());

    c.selectPaneView(rta::view::PaneSelectorButton::Eq);
    c.selectPaneView(rta::view::PaneSelectorButton::Align);
    pane = alignPaneOf(c);
    REQUIRE(pane != nullptr);
    CHECK(pane->topologyComboForTest().getSelectedId() == 2);
    CHECK(pane->highPassComboForTest().getSelectedId() == 1);
}
