// SPDX-License-Identifier: AGPL-3.0-or-later
// ALIGN-R8 reversal, PR #45 fix round 3 (PR #43 reconciliation checklist
// item 5): `TraceLibrary::clear()` (new in PR #43, Session Open's own
// library-replace step, MainComponentSession.cpp's own comment) is the
// FIRST whole-library removal path the XOVER pane's revision-gated timer
// refresh has ever had to survive -- every earlier case
// (test_main_component_panes_xover.cpp's "hiding the picked high-pass
// entry", round2's timer-revision case) only ever changed or added ONE
// entry, never removed everything at once.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "trace/Trace.h"
#include "view/CrossoverPaneView.h"

#include <juce_core/juce_core.h>

#include <string>
#include <utility>
#include <vector>

using rta::view::CrossoverPaneView;
using rta::view::PaneSelectorButton;

namespace {

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

CrossoverPaneView& selectXoverPane(MainComponent& component) {
    component.selectPaneView(PaneSelectorButton::Xover);
    auto* pane = const_cast<CrossoverPaneView*>(
        dynamic_cast<const CrossoverPaneView*>(&MainComponentTestAccess::pane(component)));
    REQUIRE(pane != nullptr);
    return *pane;
}

}  // namespace

TEST_CASE("TraceLibrary::clear() reverts a complete xover selection to refusal via the pane's own timer",
         "[main_component_panes_xover]") {
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::libraryForTest(component);
    const std::string idA = library.add(makeTestTrace("a", -6.0f), "A", "default");
    const std::string idB = library.add(makeTestTrace("b", -12.0f), "B", "default");

    auto& pane = selectXoverPane(component);
    chooseTraceById(pane.highTraceComboForTest(), pane.eligibleTraceIdsForTest(), idA);
    chooseTraceById(pane.lowTraceComboForTest(), pane.eligibleTraceIdsForTest(), idB);
    pane.topologyComboForTest().setSelectedId(1, juce::sendNotificationSync);
    pane.inversionComboForTest().setSelectedId(1, juce::sendNotificationSync);
    REQUIRE(pane.hasCompleteSelectionForTest());

    // This is exactly the call MainComponentSession::performOpen makes
    // (`library_.clear()`) before repopulating from a newly opened session --
    // called directly here, on the SAME live pane instance, so this test
    // proves the pane's OWN timer-refresh mechanism survives it, the thing
    // a full Save/Open round trip (which also rebuilds workspace_ into a
    // brand-new pane instance) could not isolate.
    library.clear();

    const auto deadline = juce::Time::getMillisecondCounter() + 2000u;
    while (pane.hasCompleteSelectionForTest() && juce::Time::getMillisecondCounter() < deadline) {
        juce::Thread::sleep(20);
        juce::Timer::callPendingTimersSynchronously();
    }

    CHECK_FALSE(pane.hasCompleteSelectionForTest());
    CHECK(pane.eligibleTraceIdsForTest().empty());
    CHECK(pane.chosenHighTraceIdForTest().empty());
    CHECK(pane.chosenLowTraceIdForTest().empty());
    // The stale surface must not survive either (test gap D, round 2) --
    // clear() is the removal path that exercises it hardest, since every
    // series the surface held came from traces that no longer exist at all.
    CHECK(pane.surfaceForTest().pointCount() == 0);
}
