// SPDX-License-Identifier: AGPL-3.0-or-later
// ALIGN-R8 reversal (owner, 2026-09-26; amendment in docs/dsp/
// 2026-09-06-l7-alignment-wizard.md): the G18 crossover surface becomes a
// live pane, reached through the 4th selector button. This file extends
// test_main_component_panes.cpp's own pane-selector coverage (that file's
// header comment explains why a REAL MainComponent is needed, not a
// hand-copied fixture) with the properties specific to XOVER: it needs a
// TraceLibrary with real entries to show anything at all, which none of the
// other three buttons do.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "trace/Trace.h"
#include "view/CrossoverPaneView.h"
#include "view/RtaView.h"

#include <juce_core/juce_core.h>

#include <utility>

using rta::view::CrossoverPaneView;
using rta::view::PaneSelectorButton;
using rta::view::PaneView;
using rta::view::RtaView;

namespace {

/// Same one-level search test_main_component_panes.cpp's own
/// `findButtonByText` uses -- `wirePaneSelectorButtons()` adds all four
/// selector buttons directly to `MainComponent`, no intervening container.
juce::Button* findButtonByText(juce::Component& root, const juce::String& text) {
    for (int i = 0; i < root.getNumChildComponents(); ++i) {
        if (auto* button = dynamic_cast<juce::Button*>(root.getChildComponent(i))) {
            if (button->getButtonText() == text) return button;
        }
    }
    return nullptr;
}

/// A minimal magnitude+phase capture -- enough for `VirtualTrace::fromTrace`
/// to accept it (it refuses a magnitude-only trace: VirtualTrace.h's own
/// class comment). The actual shape drawn does not matter to any assertion
/// below; only IDENTITY (which two ids the pane's model received) does.
rta::trace::Trace makeTestTrace(const std::string& id) {
    rta::trace::CaptureMeta meta;
    meta.id = id;
    meta.sampleRate = 48000.0;
    meta.fftSize = 64;  // pointCountFor(64) = 33: small and exact, no rounding to check.

    const std::size_t points = rta::trace::pointCountFor(meta.fftSize);
    std::vector<float> magnitudeDb(points, -6.0f);
    std::vector<float> phase(points, 0.0f);
    auto trace = rta::trace::Trace::make(meta, std::move(magnitudeDb));
    REQUIRE(trace.has_value());
    REQUIRE(trace->setPhase(std::move(phase)));
    return std::move(*trace);
}

}  // namespace

TEST_CASE("clicking XOVER with an empty library shows the crossover pane's empty state",
         "[main_component_panes_xover]") {
    MainComponent component;
    component.setSyntheticMode(true);

    component.selectPaneView(PaneSelectorButton::Xover);

    CHECK(component.currentPaneView() == PaneView::Xover);
    const auto* xoverPane = dynamic_cast<const CrossoverPaneView*>(&MainComponentTestAccess::pane(component));
    REQUIRE(xoverPane != nullptr);
    CHECK_FALSE(xoverPane->hasTwoTraces());
    // Never an RtaView masquerading as the XOVER pane -- the exact PR #26 bug
    // makePaneFactory's own test already guards for Spl, pinned here too.
    CHECK(dynamic_cast<const RtaView*>(&MainComponentTestAccess::pane(component)) == nullptr);
}

TEST_CASE("the XOVER click reached through the real button also shows the crossover pane",
         "[main_component_panes_xover]") {
    // Fix-round shape test_main_component_panes.cpp's own suite already
    // established: selectPaneView() called directly cannot tell a correctly
    // wired button from a swapped onClick. XOVER gets the same check.
    MainComponent component;
    component.setSyntheticMode(true);

    auto* xoverButton = findButtonByText(component, "XOVER");
    REQUIRE(xoverButton != nullptr);
    xoverButton->setToggleState(true, juce::sendNotification);

    CHECK(dynamic_cast<const CrossoverPaneView*>(&MainComponentTestAccess::pane(component)) != nullptr);
}

TEST_CASE("with fewer than two stored traces the crossover pane's model has none",
         "[main_component_panes_xover]") {
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::library(component);
    library.add(makeTestTrace("solo"), "Solo", "default");

    component.selectPaneView(PaneSelectorButton::Xover);

    const auto* xoverPane = dynamic_cast<const CrossoverPaneView*>(&MainComponentTestAccess::pane(component));
    REQUIRE(xoverPane != nullptr);
    CHECK_FALSE(xoverPane->hasTwoTraces());
    CHECK(xoverPane->highTraceId().empty());
    CHECK(xoverPane->lowTraceId().empty());
}

TEST_CASE("with two stored traces the crossover pane's model receives exactly those two",
         "[main_component_panes_xover]") {
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::library(component);
    const std::string highId = library.add(makeTestTrace("main-hp"), "Main HP", "default");
    const std::string lowId = library.add(makeTestTrace("sub-lp"), "Sub LP", "default");
    REQUIRE_FALSE(highId.empty());
    REQUIRE_FALSE(lowId.empty());

    component.selectPaneView(PaneSelectorButton::Xover);

    const auto* xoverPane = dynamic_cast<const CrossoverPaneView*>(&MainComponentTestAccess::pane(component));
    REQUIRE(xoverPane != nullptr);
    CHECK(xoverPane->hasTwoTraces());
    // Insertion order, not alphabetical or any other ordering -- the pane's
    // own documented "entries()[0] is the high-pass side" contract
    // (CrossoverPaneView.h), pinned against the ACTUAL ids library.add()
    // returned rather than the literals passed in, in case a future change
    // to add() ever mangled them.
    CHECK(xoverPane->highTraceId() == highId);
    CHECK(xoverPane->lowTraceId() == lowId);
    // Not just "two ids recorded" -- the model surface_ actually holds them,
    // via the one observable CrossoverSurface exposes that a synthetic trace
    // with no window/pending ops still populates: point count.
    CHECK(xoverPane->surface().pointCount() > 0);
}

TEST_CASE("a third stored trace does not change which two the crossover pane reads",
         "[main_component_panes_xover]") {
    // Guards the "[0], [1]" contract against a mutant that reads the LAST
    // two entries, or all of them, instead of the first two.
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::library(component);
    const std::string highId = library.add(makeTestTrace("main-hp"), "Main HP", "default");
    const std::string lowId = library.add(makeTestTrace("sub-lp"), "Sub LP", "default");
    library.add(makeTestTrace("extra"), "Extra", "default");

    component.selectPaneView(PaneSelectorButton::Xover);

    const auto* xoverPane = dynamic_cast<const CrossoverPaneView*>(&MainComponentTestAccess::pane(component));
    REQUIRE(xoverPane != nullptr);
    CHECK(xoverPane->highTraceId() == highId);
    CHECK(xoverPane->lowTraceId() == lowId);
}

TEST_CASE("selectPaneView keeps all four selector buttons' toggle state in sync",
         "[main_component_panes_xover]") {
    // Same cycle shape test_main_component_panes.cpp's own "keeps the
    // selector buttons' toggle state in sync" case uses, extended to the
    // 4th button -- JUCE's radio group only ever turns SIBLINGS off as a
    // side effect of turning one on, so a dropped `paneXoverButton_.
    // setToggleState(...)` line only shows up the NEXT time XOVER becomes
    // selected again, which needs a full cycle through all four to catch.
    MainComponent component;
    component.setSyntheticMode(true);

    auto* rtaButton = findButtonByText(component, "RTA");
    auto* transferButton = findButtonByText(component, "TRANSFER");
    auto* splButton = findButtonByText(component, "SPL");
    auto* xoverButton = findButtonByText(component, "XOVER");
    REQUIRE(rtaButton != nullptr);
    REQUIRE(transferButton != nullptr);
    REQUIRE(splButton != nullptr);
    REQUIRE(xoverButton != nullptr);

    component.selectPaneView(PaneSelectorButton::Xover);
    CHECK_FALSE(rtaButton->getToggleState());
    CHECK_FALSE(transferButton->getToggleState());
    CHECK_FALSE(splButton->getToggleState());
    CHECK(xoverButton->getToggleState());

    component.selectPaneView(PaneSelectorButton::Rta);
    CHECK(rtaButton->getToggleState());
    CHECK_FALSE(xoverButton->getToggleState());

    component.selectPaneView(PaneSelectorButton::Transfer);
    CHECK(transferButton->getToggleState());
    CHECK_FALSE(xoverButton->getToggleState());

    component.selectPaneView(PaneSelectorButton::Spl);
    CHECK(splButton->getToggleState());
    CHECK_FALSE(xoverButton->getToggleState());

    component.selectPaneView(PaneSelectorButton::Xover);
    CHECK_FALSE(rtaButton->getToggleState());
    CHECK_FALSE(transferButton->getToggleState());
    CHECK_FALSE(splButton->getToggleState());
    CHECK(xoverButton->getToggleState());
}
