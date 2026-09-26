// SPDX-License-Identifier: AGPL-3.0-or-later
// ALIGN-R8 reversal, PR #45 fix round 2. Split out of
// test_main_component_panes_xover.cpp (that file's own fixture/helper
// comments explain the "drive the real ComboBox" shape both files share) --
// keeping the round-1 and round-2 coverage in one file pushed it past the
// 400-line file cap (CLAUDE.md "File length"), and the round-2 findings
// (MEDIUM A, MEDIUM B, test gap D) form one coherent group on their own:
//
//   MEDIUM A -- `ready_` did not check the two trace picks were DISTINCT,
//   nor that at least two traces were eligible to pick distinctly from.
//   MEDIUM B -- the inversion answer (wizard question (c),
//   CrossoverTopology.h's own comment on `ProcessorInversion`) was
//   hardcoded rather than asked through a fourth picker.
//   Test gap D -- a numeric assertion on `surfaceForTest()` cannot tell a
//   test whether `paint()` itself still gates on `ready_`; and a stale
//   surface must not survive `ready_` dropping back to false.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "trace/Trace.h"
#include "view/CrossoverPaneView.h"

#include <juce_core/juce_core.h>

#include <numbers>
#include <string>
#include <utility>
#include <vector>

using rta::view::CrossoverPaneView;
using rta::view::PaneSelectorButton;

namespace {

/// Same shape as test_main_component_panes_xover.cpp's own `makeTestTrace` --
/// duplicated here (rather than shared through a header) because it is eight
/// lines and this file's whole reason to exist is staying under the 400-line
/// cap on its own.
rta::trace::Trace makeTestTrace(const std::string& id, float magnitudeDb) {
    rta::trace::CaptureMeta meta;
    meta.id = id;
    meta.sampleRate = 48000.0;
    meta.fftSize = 64;
    const std::size_t points = rta::trace::pointCountFor(meta.fftSize);
    std::vector<float> magnitude(points, magnitudeDb);
    auto trace = rta::trace::Trace::make(meta, std::move(magnitude));
    REQUIRE(trace.has_value());
    std::vector<float> phase(points, 0.0f);
    REQUIRE(trace->setPhase(std::move(phase)));
    return std::move(*trace);
}

/// `sendNotificationSync`, not `sendNotification` -- see
/// test_main_component_panes_xover.cpp's own `chooseTraceById` comment for
/// why (`ComboBox::sendChange`, juce_ComboBox.cpp, only pumps `onChange`
/// synchronously for `sendNotificationSync`).
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

void chooseFirstTopology(juce::ComboBox& topologyCombo) {
    topologyCombo.setSelectedId(1, juce::sendNotificationSync);
}

/// Item 1 = NOT INVERTED (`kInversionChoices`, CrossoverPaneView.cpp).
void chooseNotInverted(juce::ComboBox& inversionCombo) {
    inversionCombo.setSelectedId(1, juce::sendNotificationSync);
}

CrossoverPaneView& selectXoverPane(MainComponent& component) {
    component.selectPaneView(PaneSelectorButton::Xover);
    auto* pane = const_cast<CrossoverPaneView*>(
        dynamic_cast<const CrossoverPaneView*>(&MainComponentTestAccess::pane(component)));
    REQUIRE(pane != nullptr);
    return *pane;
}

}  // namespace

TEST_CASE("the same entry chosen as both high-pass and low-pass is refused, by name",
         "[main_component_panes_xover]") {
    // MEDIUM A: `ready_` did not check `hpId_ != lpId_`, so picking one
    // trace on both sides fed CrossoverSurface a relative phase of exactly
    // zero everywhere -- indistinguishable on screen from a correctly
    // aligned pair. Mutant: drop the `hpId_ != lpId_` half of the `ready_`
    // guard back out and this goes RED (`hasCompleteSelectionForTest()`
    // reads true with idA == idB).
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::libraryForTest(component);
    const std::string idA = library.add(makeTestTrace("a", -6.0f), "A", "default");
    library.add(makeTestTrace("b", -12.0f), "B", "default");

    auto& pane = selectXoverPane(component);
    chooseTraceById(pane.highTraceComboForTest(), pane.eligibleTraceIdsForTest(), idA);
    chooseTraceById(pane.lowTraceComboForTest(), pane.eligibleTraceIdsForTest(), idA);
    chooseFirstTopology(pane.topologyComboForTest());
    chooseNotInverted(pane.inversionComboForTest());

    CHECK_FALSE(pane.hasCompleteSelectionForTest());
    CHECK(pane.chosenHighTraceIdForTest() == idA);
    CHECK(pane.chosenLowTraceIdForTest() == idA);
}

TEST_CASE("a single eligible trace picked on both sides is refused",
         "[main_component_panes_xover]") {
    // MEDIUM A's other shape: only one eligible trace exists at all, so
    // BOTH combos can only ever select it. `ready_` must still refuse --
    // the `eligibleIds_.size() >= 2` half of the same guard.
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::libraryForTest(component);
    const std::string idA = library.add(makeTestTrace("solo", -6.0f), "Solo", "default");

    auto& pane = selectXoverPane(component);
    REQUIRE(pane.eligibleTraceIdsForTest().size() == 1);
    chooseTraceById(pane.highTraceComboForTest(), pane.eligibleTraceIdsForTest(), idA);
    chooseTraceById(pane.lowTraceComboForTest(), pane.eligibleTraceIdsForTest(), idA);
    chooseFirstTopology(pane.topologyComboForTest());
    chooseNotInverted(pane.inversionComboForTest());

    CHECK_FALSE(pane.hasCompleteSelectionForTest());
}

TEST_CASE("the inversion picker is required for a complete selection, and UNKNOWN marks the "
         "target ambiguous",
         "[main_component_panes_xover]") {
    // MEDIUM B: the round-1 cut hardcoded ProcessorInversion::No at the call
    // to setAskedTopology, which answers wizard question (c) FOR the
    // operator. Mutant: skip requiring `inversion_.has_value()` in `ready_`
    // and the first REQUIRE below goes RED (complete with nothing chosen).
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::libraryForTest(component);
    const std::string idA = library.add(makeTestTrace("a", -6.0f), "A", "default");
    const std::string idB = library.add(makeTestTrace("b", -12.0f), "B", "default");

    auto& pane = selectXoverPane(component);
    chooseTraceById(pane.highTraceComboForTest(), pane.eligibleTraceIdsForTest(), idA);
    chooseTraceById(pane.lowTraceComboForTest(), pane.eligibleTraceIdsForTest(), idB);
    chooseFirstTopology(pane.topologyComboForTest());
    CHECK_FALSE(pane.hasCompleteSelectionForTest());

    pane.inversionComboForTest().setSelectedId(1, juce::sendNotificationSync);  // NOT INVERTED
    REQUIRE(pane.hasCompleteSelectionForTest());
    CHECK_FALSE(pane.surfaceForTest().targetAmbiguous());

    // Item 3 = UNKNOWN -- the two-candidate-lines case (record Sec.13.3).
    pane.inversionComboForTest().setSelectedId(3, juce::sendNotificationSync);
    REQUIRE(pane.hasCompleteSelectionForTest());
    CHECK(pane.surfaceForTest().targetAmbiguous());
}

TEST_CASE("the chosen topology reaches the surface's target line, per topology",
         "[main_component_panes_xover]") {
    // Test gap D, adapted from the verifier's probe: LR-2 -> 180 deg, BW-1
    // -> +90 deg, LR-4 -> 0 deg (CrossoverTopology.h's own derivation).
    // Mutant v2 (topology pick ignored, a fixed LR-4 passed instead): the
    // first CHECK below (LR-2) goes RED, since a fixed LR-4 always reads 0.
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::libraryForTest(component);
    const std::string idA = library.add(makeTestTrace("a", -6.0f), "A", "default");
    const std::string idB = library.add(makeTestTrace("b", -12.0f), "B", "default");

    auto& pane = selectXoverPane(component);
    chooseTraceById(pane.highTraceComboForTest(), pane.eligibleTraceIdsForTest(), idA);
    chooseTraceById(pane.lowTraceComboForTest(), pane.eligibleTraceIdsForTest(), idB);
    chooseNotInverted(pane.inversionComboForTest());

    pane.topologyComboForTest().setSelectedId(1, juce::sendNotificationSync);  // LR-2
    REQUIRE(pane.hasCompleteSelectionForTest());
    CHECK(pane.surfaceForTest().targetRadians() == Catch::Approx(std::numbers::pi).margin(1e-9));

    pane.topologyComboForTest().setSelectedId(4, juce::sendNotificationSync);  // BW-1
    CHECK(pane.surfaceForTest().targetRadians() ==
         Catch::Approx(std::numbers::pi / 2).margin(1e-9));

    pane.topologyComboForTest().setSelectedId(2, juce::sendNotificationSync);  // LR-4
    CHECK(pane.surfaceForTest().targetRadians() == Catch::Approx(0.0).margin(1e-9));
}

TEST_CASE("paint() draws the chart only while ready, and nothing stale survives becoming unready",
         "[main_component_panes_xover]") {
    // Test gap D: `surfaceForTest()` is a numeric snapshot of the MODEL --
    // it cannot tell a test whether `paint()` ITSELF still gates on
    // `ready_`. `lastPaintDrewChartForTest()` is set inside `paint()`
    // (CrossoverPaneView.cpp), so mutant v1b (the `if (!ready_) { ...;
    // return; }` guard removed from `paint()`) is caught directly: the first
    // CHECK_FALSE below would read true, since the chart-drawing calls would
    // run unconditionally.
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::libraryForTest(component);
    const std::string idA = library.add(makeTestTrace("a", -6.0f), "A", "default");
    const std::string idB = library.add(makeTestTrace("b", -12.0f), "B", "default");

    auto& pane = selectXoverPane(component);
    juce::Image image(juce::Image::ARGB, 400, 300, true);
    {
        juce::Graphics g(image);
        pane.paint(g);
    }
    CHECK_FALSE(pane.lastPaintDrewChartForTest());

    chooseTraceById(pane.highTraceComboForTest(), pane.eligibleTraceIdsForTest(), idA);
    chooseTraceById(pane.lowTraceComboForTest(), pane.eligibleTraceIdsForTest(), idB);
    chooseFirstTopology(pane.topologyComboForTest());
    chooseNotInverted(pane.inversionComboForTest());
    REQUIRE(pane.hasCompleteSelectionForTest());
    {
        juce::Graphics g(image);
        pane.paint(g);
    }
    CHECK(pane.lastPaintDrewChartForTest());

    // Drop back to unready (hide the high-pass pick) and the NEXT paint must
    // refuse again -- the stale-surface concern the round-1 file's "hiding
    // the picked high-pass entry" case already checks at the model level
    // (`pointCount() == 0`); this checks it at the paint level too.
    REQUIRE(library.setVisible(idA, false));
    pane.setLibrary(&library);
    CHECK_FALSE(pane.hasCompleteSelectionForTest());
    {
        juce::Graphics g(image);
        pane.paint(g);
    }
    CHECK_FALSE(pane.lastPaintDrewChartForTest());
}
