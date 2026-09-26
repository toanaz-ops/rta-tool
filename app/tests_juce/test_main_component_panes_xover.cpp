// SPDX-License-Identifier: AGPL-3.0-or-later
// ALIGN-R8 reversal (owner, 2026-09-26; amendment in docs/dsp/
// 2026-09-06-l7-alignment-wizard.md): the G18 crossover surface becomes a
// live pane, reached through the 4th selector button. This file extends
// test_main_component_panes.cpp's own pane-selector coverage (that file's
// header comment explains why a REAL MainComponent is needed, not a
// hand-copied fixture) with the properties specific to XOVER.
//
// PR #45 fix round 1 (verifier, F3/F4): "which trace is which side" and
// "which topology" are ASKED, never inferred from library order or a
// hardcoded default -- this file's cases exercise the three pickers
// (CrossoverPaneView.h) directly, through the real juce::ComboBox widgets,
// the same "drive the real control, not the model underneath it" shape
// test_main_component_panes.cpp's own button tests already use.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "trace/Trace.h"
#include "view/CrossoverPaneView.h"
#include "view/RtaView.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

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

/// A magnitude(+optional phase) capture. `magnitudeDb` is a FLAT, DISTINCT
/// constant per caller so a test can pin WHICH trace's DATA the model
/// actually used, not only which id string a picker reports.
/// `withPhase = false` builds the magnitude-only shape
/// `VirtualTrace::fromTrace` refuses (VirtualTrace.h's own class comment) --
/// used to prove such an entry is excluded from the pickers outright.
rta::trace::Trace makeTestTrace(const std::string& id, float magnitudeDb, bool withPhase = true) {
    rta::trace::CaptureMeta meta;
    meta.id = id;
    meta.sampleRate = 48000.0;
    meta.fftSize = 64;  // pointCountFor(64) = 33: small and exact, no rounding to check.

    const std::size_t points = rta::trace::pointCountFor(meta.fftSize);
    std::vector<float> magnitude(points, magnitudeDb);
    auto trace = rta::trace::Trace::make(meta, std::move(magnitude));
    REQUIRE(trace.has_value());
    if (withPhase) {
        std::vector<float> phase(points, 0.0f);
        REQUIRE(trace->setPhase(std::move(phase)));
    }
    return std::move(*trace);
}

/// Selects `traceId` on `combo` by scanning `eligibleIds` for its index --
/// the real UI path. `sendNotificationSync`, NOT `sendNotification`:
/// `ComboBox::sendChange` (juce_ComboBox.cpp) only calls
/// `handleUpdateNowIfNeeded()` for `sendNotificationSync` specifically --
/// plain `sendNotification` merely posts an async update through the
/// message loop this offscreen test never pumps, so `onChange` would never
/// fire at all. Button's `setToggleState` does not have this trap (it calls
/// `sendClickMessage()` unconditionally, synchronously, for any
/// notification other than `dontSendNotification` -- test_main_component_
/// panes.cpp's own button tests rely on exactly that), which is why this
/// file's one button case below still uses plain `sendNotification`.
void chooseTraceById(juce::ComboBox& combo, const std::vector<std::string>& eligibleIds,
                     const std::string& traceId) {
    for (std::size_t i = 0; i < eligibleIds.size(); ++i) {
        if (eligibleIds[i] == traceId) {
            combo.setSelectedId(static_cast<int>(i) + 1, juce::sendNotificationSync);
            return;
        }
    }
    FAIL("traceId not found among eligible ids: " << traceId);
}

/// Picks the topology combo's first item ("LR-2") -- the exact choice never
/// matters to a case that only checks "is a target line now drawn", so every
/// case that needs SOME topology picks the same one rather than repeating
/// the item-1 magic number at each call site.
void chooseFirstTopology(juce::ComboBox& topologyCombo) {
    topologyCombo.setSelectedId(1, juce::sendNotificationSync);
}

CrossoverPaneView& selectXoverPane(MainComponent& component) {
    component.selectPaneView(PaneSelectorButton::Xover);
    // MainComponentTestAccess::pane() returns a const ref (the pane's
    // owning workspace_ is not exposed for mutation); the ComboBox test
    // hooks below are non-const because they hand back a live widget a test
    // drives, so a const_cast here is the one place that gap is bridged.
    auto* pane = const_cast<CrossoverPaneView*>(
        dynamic_cast<const CrossoverPaneView*>(&MainComponentTestAccess::pane(component)));
    REQUIRE(pane != nullptr);
    return *pane;
}

}  // namespace

TEST_CASE("clicking XOVER with an empty library shows the crossover pane's refusal state",
         "[main_component_panes_xover]") {
    MainComponent component;
    component.setSyntheticMode(true);

    auto& pane = selectXoverPane(component);

    CHECK(component.currentPaneView() == PaneView::Xover);
    CHECK_FALSE(pane.hasCompleteSelectionForTest());
    CHECK(pane.eligibleTraceIdsForTest().empty());
    // Never an RtaView masquerading as the XOVER pane -- the exact PR #26 bug
    // makePaneFactory's own test already guards for Spl, pinned here too.
    CHECK(dynamic_cast<const RtaView*>(&MainComponentTestAccess::pane(component)) == nullptr);
}

TEST_CASE("the XOVER click reached through the real button also shows the crossover pane",
         "[main_component_panes_xover]") {
    MainComponent component;
    component.setSyntheticMode(true);

    auto* xoverButton = findButtonByText(component, "XOVER");
    REQUIRE(xoverButton != nullptr);
    xoverButton->setToggleState(true, juce::sendNotification);

    CHECK(dynamic_cast<const CrossoverPaneView*>(&MainComponentTestAccess::pane(component)) != nullptr);
}

TEST_CASE("a phase-less entry is excluded from the pickers even with valid entries around it",
         "[main_component_panes_xover]") {
    // Mutant: a filter that forgets the phase check would include "no-phase"
    // at index 0 alongside the two valid ones.
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::libraryForTest(component);
    library.add(makeTestTrace("no-phase", -6.0f, /*withPhase=*/false), "No Phase", "default");
    const std::string idA = library.add(makeTestTrace("a", -6.0f), "A", "default");
    const std::string idB = library.add(makeTestTrace("b", -12.0f), "B", "default");

    auto& pane = selectXoverPane(component);

    const auto& eligible = pane.eligibleTraceIdsForTest();
    CHECK(eligible.size() == 2);
    CHECK(std::find(eligible.begin(), eligible.end(), "no-phase") == eligible.end());
    CHECK(std::find(eligible.begin(), eligible.end(), idA) != eligible.end());
    CHECK(std::find(eligible.begin(), eligible.end(), idB) != eligible.end());
}

TEST_CASE("a hidden entry is excluded from the pickers", "[main_component_panes_xover]") {
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::libraryForTest(component);
    const std::string hiddenId = library.add(makeTestTrace("hidden", -6.0f), "Hidden", "default");
    const std::string visibleId = library.add(makeTestTrace("visible", -12.0f), "Visible", "default");
    REQUIRE(library.setVisible(hiddenId, false));

    auto& pane = selectXoverPane(component);

    const auto& eligible = pane.eligibleTraceIdsForTest();
    CHECK(eligible.size() == 1);
    CHECK(std::find(eligible.begin(), eligible.end(), hiddenId) == eligible.end());
    CHECK(std::find(eligible.begin(), eligible.end(), visibleId) != eligible.end());
}

TEST_CASE("with fewer than two eligible traces the refusal names the true eligible count",
         "[main_component_panes_xover]") {
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::libraryForTest(component);
    // One phase-bearing entry and one magnitude-only entry: the total entry
    // count is 2, but the ELIGIBLE count -- what the refusal must name -- is
    // 1 (PR #45 fix round F3: "give the true count of eligible traces, not
    // the total entry count").
    library.add(makeTestTrace("solo", -6.0f), "Solo", "default");
    library.add(makeTestTrace("no-phase", -6.0f, /*withPhase=*/false), "No Phase", "default");

    auto& pane = selectXoverPane(component);

    CHECK_FALSE(pane.hasCompleteSelectionForTest());
    CHECK(pane.eligibleTraceIdsForTest().size() == 1);
}

TEST_CASE("with two traces chosen but no topology, no target line is drawn",
         "[main_component_panes_xover]") {
    // "No target line is drawn" is this pane's refusal state -- ready_ false
    // -- which paint() checks before calling into CrossoverSurfaceRenderer
    // at all (CrossoverPaneView.cpp's own paint()).
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::libraryForTest(component);
    const std::string idA = library.add(makeTestTrace("a", -6.0f), "A", "default");
    const std::string idB = library.add(makeTestTrace("b", -12.0f), "B", "default");

    auto& pane = selectXoverPane(component);
    chooseTraceById(pane.highTraceComboForTest(), pane.eligibleTraceIdsForTest(), idA);
    chooseTraceById(pane.lowTraceComboForTest(), pane.eligibleTraceIdsForTest(), idB);

    CHECK(pane.chosenHighTraceIdForTest() == idA);
    CHECK(pane.chosenLowTraceIdForTest() == idB);
    CHECK_FALSE(pane.hasCompleteSelectionForTest());
}

TEST_CASE("after the operator picks a non-first pair, the model receives exactly that pair",
         "[main_component_panes_xover]") {
    // Three eligible entries; the operator picks the 2nd and 3rd, never the
    // 1st -- guards the "ids, not library order" contract against a mutant
    // that silently reads entries()[0]/[1] regardless of what was picked.
    constexpr float kFirstMagnitudeDb = -30.0f;   // never picked
    constexpr float kHighMagnitudeDb = -6.0f;
    constexpr float kLowMagnitudeDb = -18.0f;

    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::libraryForTest(component);
    library.add(makeTestTrace("first", kFirstMagnitudeDb), "First", "default");
    const std::string highId = library.add(makeTestTrace("main-hp", kHighMagnitudeDb), "Main HP", "default");
    const std::string lowId = library.add(makeTestTrace("sub-lp", kLowMagnitudeDb), "Sub LP", "default");

    auto& pane = selectXoverPane(component);
    chooseTraceById(pane.highTraceComboForTest(), pane.eligibleTraceIdsForTest(), highId);
    chooseTraceById(pane.lowTraceComboForTest(), pane.eligibleTraceIdsForTest(), lowId);
    chooseFirstTopology(pane.topologyComboForTest());

    REQUIRE(pane.hasCompleteSelectionForTest());
    CHECK(pane.chosenHighTraceIdForTest() == highId);
    CHECK(pane.chosenLowTraceIdForTest() == lowId);
    // The DATA itself, not just the id label.
    REQUIRE(pane.surfaceForTest().highSideDb().size() > 1);
    REQUIRE(pane.surfaceForTest().lowSideDb().size() > 1);
    CHECK(pane.surfaceForTest().highSideDb()[1] == Catch::Approx(kHighMagnitudeDb).margin(0.05));
    CHECK(pane.surfaceForTest().lowSideDb()[1] == Catch::Approx(kLowMagnitudeDb).margin(0.05));
}

TEST_CASE("hiding the picked high-pass entry resets that picker to not asked",
         "[main_component_panes_xover]") {
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::libraryForTest(component);
    const std::string highId = library.add(makeTestTrace("main-hp", -6.0f), "Main HP", "default");
    const std::string lowId = library.add(makeTestTrace("sub-lp", -18.0f), "Sub LP", "default");

    auto& pane = selectXoverPane(component);
    chooseTraceById(pane.highTraceComboForTest(), pane.eligibleTraceIdsForTest(), highId);
    chooseTraceById(pane.lowTraceComboForTest(), pane.eligibleTraceIdsForTest(), lowId);
    chooseFirstTopology(pane.topologyComboForTest());
    REQUIRE(pane.hasCompleteSelectionForTest());

    REQUIRE(library.setVisible(highId, false));
    // The library change is picked up on the next revision-gated refresh --
    // driven here the same way setLibrary/selectPaneView already do
    // (refreshFromLibrary is not timer-only), by re-pointing the pane at the
    // same library, which is exactly what a real revision-driven refresh
    // also does.
    pane.setLibrary(&library);

    CHECK(pane.chosenHighTraceIdForTest().empty());
    CHECK_FALSE(pane.hasCompleteSelectionForTest());
    // The low-pass pick, untouched by the high-pass hide, must survive.
    CHECK(pane.chosenLowTraceIdForTest() == lowId);
}

TEST_CASE("a library revision change is picked up by the pane's own timer",
         "[main_component_panes_xover]") {
    // PR #45 fix round LOW F6: proves the TIMER path, not just setLibrary()
    // being called again -- the shape a real operator's edit (through some
    // other UI, with this pane merely sitting on screen) actually takes.
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::libraryForTest(component);
    const std::string highId = library.add(makeTestTrace("main-hp", -6.0f), "Main HP", "default");
    const std::string lowId = library.add(makeTestTrace("sub-lp", -18.0f), "Sub LP", "default");

    auto& pane = selectXoverPane(component);
    chooseTraceById(pane.highTraceComboForTest(), pane.eligibleTraceIdsForTest(), highId);
    chooseTraceById(pane.lowTraceComboForTest(), pane.eligibleTraceIdsForTest(), lowId);
    chooseFirstTopology(pane.topologyComboForTest());
    REQUIRE(pane.hasCompleteSelectionForTest());

    // Change the library WITHOUT calling setLibrary again -- library.add
    // below is a third entry, which does not disturb the two already
    // picked, but DOES advance revision(), which is what the timer alone
    // must notice.
    library.add(makeTestTrace("extra", -30.0f), "Extra", "default");

    // The background timer thread (juce_Timer.cpp's own TimerThread)
    // decrements every live juce::Timer's countdown in real wall-clock time
    // on its own schedule, independent of this test -- polling
    // callPendingTimersSynchronously alongside a short sleep, the same
    // "wait for an async effect, don't guess one fixed delay" shape
    // test_main_component_panes.cpp's own waitUntil() already uses for
    // AnalysisThread's async requests, is what makes this deterministic
    // rather than a race against however much wall-clock time the earlier
    // combo selections already spent.
    const auto deadline = juce::Time::getMillisecondCounter() + 2000u;
    while (pane.eligibleTraceIdsForTest().size() < 3 && juce::Time::getMillisecondCounter() < deadline) {
        juce::Thread::sleep(20);
        juce::Timer::callPendingTimersSynchronously();
    }

    CHECK(pane.eligibleTraceIdsForTest().size() == 3);
    // The picks themselves are untouched by an unrelated addition.
    CHECK(pane.chosenHighTraceIdForTest() == highId);
    CHECK(pane.chosenLowTraceIdForTest() == lowId);
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
