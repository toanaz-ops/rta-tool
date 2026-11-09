// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests_juce. HW-176 (owner card = A, 2026-11-09): the
// `+ PANE` / `- PANE` selector against a REAL MainComponent. The pure rules are
// proven in app/tests/test_pane_selector_decision.cpp; this file proves they
// are WIRED -- the real buttons' onClick, the real rebuild, the one-shot arm,
// the lit set, the enable state, and STORE freezing one trace per kind.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "trace/Workspace.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <string>
#include <vector>

using rta::view::PaneSelectorButton;
using rta::view::PaneView;

namespace {

// The rail's rows live in a scroll viewport (PR #102), so a lookup descends.
juce::Button* findButton(juce::Component& root, const juce::String& text) {
    std::function<juce::Button*(juce::Component&)> walk = [&](juce::Component& node) -> juce::Button* {
        for (int i = 0; i < node.getNumChildComponents(); ++i) {
            auto* child = node.getChildComponent(i);
            if (auto* button = dynamic_cast<juce::Button*>(child)) {
                if (button->getButtonText() == text) return button;
            }
            if (auto* found = walk(*child)) return found;
        }
        return nullptr;
    };
    auto* found = walk(root);
    INFO("button \"" << text.toStdString() << "\" not found");
    REQUIRE(found != nullptr);
    return found;
}

std::vector<std::string> viewsOnScreen(const MainComponent& c) {
    std::vector<std::string> out;
    for (const auto& spec : MainComponentTestAccess::paneSpecsForTest(c)) out.push_back(spec.view);
    return out;
}

using Views = std::vector<std::string>;

}  // namespace

TEST_CASE("+ PANE arms one append: the next view press adds, then the arm clears",
         "[main_component_pane_multi]") {
    MainComponent c;
    auto* add = findButton(c, "+ PANE");
    auto* transfer = findButton(c, "TRANSFER");
    auto* spl = findButton(c, "SPL");

    add->onClick();
    CHECK(MainComponentTestAccess::paneAddArmedForTest(c));
    CHECK(add->getToggleState());
    transfer->onClick();
    CHECK(viewsOnScreen(c) == Views{"rta", "transfer"});

    // One-shot: the arm is spent, so this press REPLACES instead of adding a third.
    CHECK_FALSE(MainComponentTestAccess::paneAddArmedForTest(c));
    CHECK_FALSE(add->getToggleState());
    spl->onClick();
    CHECK(viewsOnScreen(c) == Views{"spl"});
}

TEST_CASE("duplicates are allowed and every present view is lit", "[main_component_pane_multi]") {
    MainComponent c;
    auto* add = findButton(c, "+ PANE");
    findButton(c, "TRANSFER")->onClick();  // replace: [transfer]
    add->onClick();
    findButton(c, "TRANSFER")->onClick();  // [transfer, transfer]
    CHECK(viewsOnScreen(c) == Views{"transfer", "transfer"});
    add->onClick();
    findButton(c, "XOVER")->onClick();  // [transfer, transfer, xover]
    CHECK(viewsOnScreen(c) == Views{"transfer", "transfer", "xover"});

    CHECK(findButton(c, "TRANSFER")->getToggleState());
    CHECK(findButton(c, "XOVER")->getToggleState());
    CHECK_FALSE(findButton(c, "RTA")->getToggleState());
    CHECK_FALSE(findButton(c, "SPL")->getToggleState());
}

TEST_CASE("+ PANE is disabled at the cap and - PANE at one pane", "[main_component_pane_multi]") {
    MainComponent c;
    auto* add = findButton(c, "+ PANE");
    auto* remove = findButton(c, "- PANE");
    REQUIRE(rta::trace::kMaxPanes == 3);

    CHECK(add->isEnabled());
    CHECK_FALSE(remove->isEnabled());  // one pane: nothing to remove

    add->onClick();
    findButton(c, "TRANSFER")->onClick();  // 2 panes
    CHECK(add->isEnabled());
    CHECK(remove->isEnabled());

    add->onClick();
    findButton(c, "SPL")->onClick();  // 3 panes = cap
    REQUIRE(viewsOnScreen(c).size() == 3u);
    CHECK_FALSE(add->isEnabled());
    CHECK(remove->isEnabled());

    // A refused add states why, and leaves the layout alone.
    add->onClick();
    CHECK(viewsOnScreen(c).size() == 3u);
    CHECK(MainComponentTestAccess::paneReadoutForTest(c).contains("at most 3"));
    CHECK_FALSE(MainComponentTestAccess::paneAddArmedForTest(c));
}

TEST_CASE("- PANE removes the last pane and re-enables + PANE", "[main_component_pane_multi]") {
    MainComponent c;
    auto* add = findButton(c, "+ PANE");
    auto* remove = findButton(c, "- PANE");
    for (const char* view : {"TRANSFER", "SPL"}) {
        add->onClick();
        findButton(c, view)->onClick();
    }
    REQUIRE(viewsOnScreen(c) == Views{"rta", "transfer", "spl"});

    remove->onClick();
    CHECK(viewsOnScreen(c) == Views{"rta", "transfer"});
    CHECK(add->isEnabled());
    CHECK_FALSE(findButton(c, "SPL")->getToggleState());

    remove->onClick();
    CHECK(viewsOnScreen(c) == Views{"rta"});
    CHECK_FALSE(remove->isEnabled());
    // Refused at one pane: said, not silent, and the pane stays.
    remove->onClick();
    CHECK(viewsOnScreen(c) == Views{"rta"});
    CHECK(MainComponentTestAccess::paneReadoutForTest(c).contains("cannot be removed"));
}

TEST_CASE("a plain view press in a multi-pane workspace collapses to that one pane",
         "[main_component_pane_multi]") {
    MainComponent c;
    findButton(c, "+ PANE")->onClick();
    findButton(c, "XOVER")->onClick();
    REQUIRE(viewsOnScreen(c).size() == 2u);
    findButton(c, "EQ")->onClick();
    CHECK(viewsOnScreen(c) == Views{"eq"});
}

TEST_CASE("STORE is enabled when any pane is RTA or TRANSFER, not only the first (Z6)",
         "[main_component_pane_multi]") {
    MainComponent c;
    c.setSyntheticMode(true);
    MainComponentTestAccess::restoreWorkspaceForTest(
        c, {rta::trace::PaneSpec{"xover"}, rta::trace::PaneSpec{"transfer"}});
    CHECK(MainComponentTestAccess::storeButtonEnabledForTest(c));
    MainComponentTestAccess::restoreWorkspaceForTest(
        c, {rta::trace::PaneSpec{"xover"}, rta::trace::PaneSpec{"spl"}});
    CHECK_FALSE(MainComponentTestAccess::storeButtonEnabledForTest(c));
}
