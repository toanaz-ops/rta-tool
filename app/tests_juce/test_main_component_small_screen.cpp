// SPDX-License-Identifier: AGPL-3.0-or-later
// H8 review item 6a: windowSizingFor() was tested only as arithmetic. This
// builds the real MainComponent at the content sizes a 1366x768 laptop gets and
// at the enforced minimum, and checks the controls the UI test lost at
// 1000x599 -- the masthead's GEN/OUT cells and the rail's START button.
//
// Sizes. Main.cpp uses a NATIVE title bar (setUsingNativeTitleBar(true)), and
// windowSizingFor() returns the WINDOW size including that bar; the content is
// window minus the bar. kTitleBar = 30 is the Windows 10/11 figure at 100 %
// scale -- an assumption, but a conservative one for the 1366x728 work area
// (a taller bar only shrinks the content further, and 40 px of slack is
// asserted nowhere). The minimum applies to the window too, so it loses the
// same bar.
//
// What is asserted, and what is deliberately NOT:
//  - masthead GEN/OUT cells fully inside the component and not overlapping;
//  - DEVICE panel and START inside the rail's scrollable content, i.e.
//    reachable BY SCROLLING;
//  - NOT "START visible without scrolling": measured 2026-11-09 it is not (the
//    rail viewport is only 168 px tall at 1300x698 and 80 px at 1100x610,
//    START starts at content y=156). The REGRESSION LOCK case records that so
//    a layout fix flips it knowingly. Owner decision pending (H8 plan, 6a).

#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "WindowSizing.h"

#include <array>

namespace {

constexpr int kTitleBar = 30;  // native Windows title bar, 100 % scale (see header)

struct Size {
    int w;
    int h;
};

struct Rig {
    explicit Rig(Size content) {
        component.setSize(content.w, content.h);
        component.resized();  // no desktop peer: setSize() alone does not lay out
    }
    MainComponent component;
};

void checkMastheadCells(Size content) {
    INFO("content " << content.w << "x" << content.h);
    Rig rig(content);
    auto& gen = MainComponentTestAccess::generatorForTest(rig.component);
    const std::array<const juce::Component*, 7> cells{
        &gen.genButtonForTest(),        &gen.minusButtonForTest(),      &gen.plusButtonForTest(),
        &gen.signalComboForTest(),      &gen.frequencyComboForTest(),   &gen.outButtonForTest(),
        &gen.measButtonForTest()};
    const juce::Component& readout = gen.readoutComponentForTest();
    const auto bounds = rig.component.getLocalBounds();
    for (const juce::Component* cell : cells) {
        CHECK(cell->getWidth() > 0);
        CHECK(bounds.contains(cell->getBounds()));
    }
    CHECK(readout.getWidth() > 0);
    CHECK(bounds.contains(readout.getBounds()));
    // No two cells overlap (the strip is laid out left to right with gaps).
    for (std::size_t i = 0; i < cells.size(); ++i) {
        for (std::size_t j = i + 1; j < cells.size(); ++j) {
            CHECK_FALSE(cells[i]->getBounds().intersects(cells[j]->getBounds()));
        }
        CHECK_FALSE(cells[i]->getBounds().intersects(readout.getBounds()));
    }
}

/// START in the rail content's coordinates (START is a child of the panel,
/// the panel a child of the scrolled content).
juce::Rectangle<int> startInContent(const MainComponent& component) {
    const juce::Component& panel = MainComponentTestAccess::devicePanelForTest(component);
    const juce::Component& start = MainComponentTestAccess::devicePanelTypedForTest(component).startStopButtonForTest();
    return start.getBounds() + panel.getPosition();
}

void checkRailReachableByScrolling(Size content) {
    INFO("content " << content.w << "x" << content.h);
    Rig rig(content);
    const juce::Viewport& viewport = MainComponentTestAccess::railViewportForTest(rig.component);
    const juce::Component& railContent = MainComponentTestAccess::railContentForTest(rig.component);
    const juce::Component& panel = MainComponentTestAccess::devicePanelForTest(rig.component);
    REQUIRE(viewport.getViewedComponent() == &railContent);
    CHECK_FALSE(panel.getBounds().isEmpty());
    CHECK(MainComponentTestAccess::devicePanelTypedForTest(rig.component).startStopButtonForTest().isVisible());
    const auto start = startInContent(rig.component);
    CHECK_FALSE(start.isEmpty());
    CHECK(railContent.getLocalBounds().contains(start));
    CHECK(railContent.getLocalBounds().contains(panel.getBounds()));
}

}  // namespace

TEST_CASE("small screen: 1366x768 laptop content keeps the masthead GEN/OUT cells on screen",
          "[main_component_small_screen]") {
    const auto sizing = rta::app::windowSizingFor(1366, 768 - 40);  // 40 px taskbar
    checkMastheadCells({sizing.width, sizing.height - kTitleBar});
}

TEST_CASE("small screen: the enforced minimum window keeps the masthead GEN/OUT cells on screen",
          "[main_component_small_screen]") {
    checkMastheadCells({rta::app::kMinimumWidth, rta::app::kMinimumHeight - kTitleBar});
}

TEST_CASE("small screen: DEVICE panel and START are reachable by scrolling at 1366x768 and the minimum",
          "[main_component_small_screen]") {
    const auto sizing = rta::app::windowSizingFor(1366, 768 - 40);
    checkRailReachableByScrolling({sizing.width, sizing.height - kTitleBar});
    checkRailReachableByScrolling({rta::app::kMinimumWidth, rta::app::kMinimumHeight - kTitleBar});
}

// REGRESSION LOCK, not a correctness claim: records the MEASURED state on
// 2026-11-09 that START is mostly cut off without scrolling. A layout fix that
// makes START visible turns this red on purpose; flip it to a plain visibility
// assertion then.
TEST_CASE("small screen: REGRESSION LOCK -- START is not fully visible without scrolling",
          "[main_component_small_screen]") {
    const auto sizing = rta::app::windowSizingFor(1366, 768 - 40);
    {
        Rig rig({sizing.width, sizing.height - kTitleBar});  // 1300x698
        const auto& viewport = MainComponentTestAccess::railViewportForTest(rig.component);
        const auto start = startInContent(rig.component);
        const int shown = viewport.getViewArea().getIntersection(start).getHeight();
        CHECK(shown > 0);              // ~12 of 62 px
        CHECK(shown < start.getHeight());
    }
    {
        Rig rig({rta::app::kMinimumWidth, rta::app::kMinimumHeight - kTitleBar});  // 1100x610
        const auto& viewport = MainComponentTestAccess::railViewportForTest(rig.component);
        CHECK_FALSE(viewport.getViewArea().intersects(startInContent(rig.component)));  // hidden
    }
}
