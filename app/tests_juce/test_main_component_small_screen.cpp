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
//  - HW-190 (owner decision 2026-11-09): the DEVICE block is pinned to the top of
//    the rail, outside the scroll viewport, so START is FULLY visible with no
//    scrolling at 1300x698 and 1100x610. "Visible" is checked against every
//    ancestor's clip, not just the component bounds: a START inside a viewport
//    that hides it fails.
//  - the rail's scrolled content (TF picker, routing matrix, role table) still
//    has a viewport to scroll in, i.e. pinning the panel did not squeeze it away.
//  Before HW-190, measured 2026-11-09: START showed ~12 of 62 px at 1300x698
//  (viewport 168 px tall) and was hidden at the minimum, 1100x610.

#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "WindowSizing.h"

#include <array>
#include <cmath>
#include <functional>

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

/// True when `child` is fully on screen inside `root`: at every ancestor up to
/// `root`, the child's rectangle (in that ancestor's coordinates) lies inside
/// the ancestor's own bounds. A viewport's clip holder is such an ancestor, so
/// a control scrolled or cut out of a viewport's view fails here.
bool fullyVisibleIn(const juce::Component& root, const juce::Component& child) {
    juce::Rectangle<int> rect = child.getLocalBounds();
    for (const juce::Component* c = &child; c != &root; c = c->getParentComponent()) {
        if (c->getParentComponent() == nullptr) return false;  // not under root
        rect += c->getPosition();
        if (!c->getParentComponent()->getLocalBounds().contains(rect)) return false;
    }
    return true;
}

void checkStartFullyVisible(Size content) {
    INFO("content " << content.w << "x" << content.h);
    Rig rig(content);
    const juce::Component& start =
        MainComponentTestAccess::devicePanelTypedForTest(rig.component).startStopButtonForTest();
    REQUIRE(start.isVisible());
    CHECK(start.getHeight() > 0);
    CHECK(fullyVisibleIn(rig.component, start));

    // Pinning the panel must not have squeezed the scrolled rail to nothing.
    const juce::Viewport& viewport = MainComponentTestAccess::railViewportForTest(rig.component);
    const juce::Component& railContent = MainComponentTestAccess::railContentForTest(rig.component);
    REQUIRE(viewport.getViewedComponent() == &railContent);
    CHECK(viewport.getHeight() > 0);
    CHECK(viewport.getBottom() <= rig.component.getHeight());
    // The pinned panel sits above the viewport and overlaps nothing of it.
    const juce::Component& panel = MainComponentTestAccess::devicePanelForTest(rig.component);
    CHECK(panel.getBottom() <= viewport.getY());
    // The scrolled content is no shorter than the viewport that scrolls it.
    CHECK(railContent.getHeight() >= viewport.getHeight());
}

/// HW-176: the two pane-edit buttons sit in the selector row, which has to hold
/// nine cells now. They must be on screen, wide enough to show their text, and
/// overlap nothing in that row.
void checkPaneEditButtons(Size content) {
    INFO("content " << content.w << "x" << content.h);
    Rig rig(content);
    const auto find = [&rig](const juce::String& text) -> juce::Button* {
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
        auto* found = walk(rig.component);
        REQUIRE(found != nullptr);
        return found;
    };
    const juce::Button* add = find("+ PANE");
    const juce::Button* remove = find("- PANE");
    const juce::Button* rt60 = find("RT60");
    for (const juce::Button* b : {add, remove}) {
        CHECK(b->isVisible());
        CHECK(b->getWidth() >= 50);  // wide enough for "+ PANE" at the row's font
        CHECK(fullyVisibleIn(rig.component, *b));
    }
    CHECK_FALSE(add->getBounds().intersects(remove->getBounds()));
    CHECK_FALSE(add->getBounds().intersects(rt60->getBounds()));
    CHECK(add->getY() == rt60->getY());  // one row

    // Every view cell's label fits its cell at the row's own font (the cells were
    // narrowed to make room for IR; TRANSFER is the widest label). Measured with
    // the look-and-feel's font, not a guessed pixel count.
    for (const char* label : {"RTA", "TRANSFER", "SPL", "XOVER", "EQ", "ALIGN", "RT60", "IR"}) {
        auto* cell = dynamic_cast<juce::TextButton*>(find(label));
        REQUIRE(cell != nullptr);
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(cell->getLookAndFeel().getTextButtonFont(*cell, cell->getHeight()), cell->getButtonText(),
                             0.0f, 0.0f);
        const int textWidth = static_cast<int>(std::ceil(glyphs.getBoundingBox(0, -1, true).getWidth()));
        INFO(label << ": text " << textWidth << " px in a " << cell->getWidth() << " px cell");
        CHECK(textWidth <= cell->getWidth() - 8);  // 4 px of air each side
    }
}

}  // namespace

TEST_CASE("small screen: + PANE / - PANE are on screen at 1366x768 and the minimum",
          "[main_component_small_screen]") {
    const auto sizing = rta::app::windowSizingFor(1366, 768 - 40);
    checkPaneEditButtons({sizing.width, sizing.height - kTitleBar});
    checkPaneEditButtons({rta::app::kMinimumWidth, rta::app::kMinimumHeight - kTitleBar});
}

TEST_CASE("small screen: 1366x768 laptop content keeps the masthead GEN/OUT cells on screen",
          "[main_component_small_screen]") {
    const auto sizing = rta::app::windowSizingFor(1366, 768 - 40);  // 40 px taskbar
    checkMastheadCells({sizing.width, sizing.height - kTitleBar});
}

TEST_CASE("small screen: the enforced minimum window keeps the masthead GEN/OUT cells on screen",
          "[main_component_small_screen]") {
    checkMastheadCells({rta::app::kMinimumWidth, rta::app::kMinimumHeight - kTitleBar});
}

TEST_CASE("small screen: START is fully visible without scrolling at 1366x768 and the minimum (HW-190)",
          "[main_component_small_screen]") {
    const auto sizing = rta::app::windowSizingFor(1366, 768 - 40);
    checkStartFullyVisible({sizing.width, sizing.height - kTitleBar});  // 1300x698
    checkStartFullyVisible({rta::app::kMinimumWidth, rta::app::kMinimumHeight - kTitleBar});  // 1100x610
}
