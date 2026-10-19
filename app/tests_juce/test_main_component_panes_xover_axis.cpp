// SPDX-License-Identifier: AGPL-3.0-or-later
// SWEEP lane task T7 (docs/plans/2026-09-29-sweep-ir-lane-plan.md): the XOVER
// pickers refuse a pair of traces whose frequency axes differ. A SWEEP trace
// (32768 points at 48 kHz) and a STORE trace (4096) index bin k at different
// frequencies; `sumOf` pairs them by position, so without this guard the pane
// would draw a sum of unrelated bins as if it were a crossover.
//
// Real MainComponent and real combo widgets, the same shape as
// test_main_component_panes_xover.cpp. The sizes are the plan's own, 4096 and
// 32768: a trace is `fftSize / 2 + 1` floats, so these are 2049 and 16385
// points -- small enough to build in a test.
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

rta::trace::Trace makePhaseTrace(const std::string& id, int fftSize, double sampleRate = 48000.0) {
    rta::trace::CaptureMeta meta;
    meta.id = id;
    meta.sampleRate = sampleRate;
    meta.fftSize = fftSize;
    const std::size_t points = rta::trace::pointCountFor(fftSize);
    auto trace = rta::trace::Trace::make(meta, std::vector<float>(points, -6.0f));
    REQUIRE(trace.has_value());
    REQUIRE(trace->setPhase(std::vector<float>(points, 0.0f)));
    return std::move(*trace);
}

void pick(juce::ComboBox& combo, const std::vector<std::string>& ids, const std::string& id) {
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == id) {
            combo.setSelectedId(static_cast<int>(i) + 1, juce::sendNotificationSync);
            return;
        }
    }
    FAIL("trace id not eligible: " << id);
}

// Selects both traces plus topology and inversion, so the ONLY thing that can
// still refuse the pair is the axis.
CrossoverPaneView& pickPair(MainComponent& component, const std::string& high, const std::string& low) {
    component.selectPaneView(PaneSelectorButton::Xover);
    auto* pane = const_cast<CrossoverPaneView*>(
        dynamic_cast<const CrossoverPaneView*>(&MainComponentTestAccess::pane(component)));
    REQUIRE(pane != nullptr);
    pick(pane->highTraceComboForTest(), pane->eligibleTraceIdsForTest(), high);
    pick(pane->lowTraceComboForTest(), pane->eligibleTraceIdsForTest(), low);
    pane->topologyComboForTest().setSelectedId(1, juce::sendNotificationSync);
    pane->inversionComboForTest().setSelectedId(1, juce::sendNotificationSync);
    return *pane;
}

}  // namespace

TEST_CASE("xover axis: a 4096-point trace against a 32768-point trace is refused, both sizes named",
          "[main_component_panes_xover_axis]") {
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::libraryForTest(component);
    const auto storeId = library.add(makePhaseTrace("store", 4096), "TRANSFER @ 10:00:00", "TRANSFER");
    const auto sweepId = library.add(makePhaseTrace("sweep", 32768), "SWEEP @ 10:00:05", "SWEEP");

    auto& pane = pickPair(component, storeId, sweepId);

    CHECK_FALSE(pane.hasCompleteSelectionForTest());
    // No surface: a stale or half-built one must not survive the refusal.
    CHECK(pane.surfaceForTest().highSideDb().empty());
    const juce::String message = pane.refusalMessageForTest();
    CHECK(message.contains("4096-POINT"));
    CHECK(message.contains("32768-POINT"));
    pane.setSize(600, 400);
    pane.createComponentSnapshot(pane.getLocalBounds());  // paints: must take the refusal branch
    CHECK_FALSE(pane.lastPaintDrewChartForTest());
}

TEST_CASE("xover axis: two traces of the same size are drawn", "[main_component_panes_xover_axis]") {
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::libraryForTest(component);
    const auto a = library.add(makePhaseTrace("sweep-a", 32768), "SWEEP @ 10:00:05", "SWEEP");
    const auto b = library.add(makePhaseTrace("sweep-b", 32768), "SWEEP @ 10:01:05", "SWEEP");

    auto& pane = pickPair(component, a, b);

    CHECK(pane.hasCompleteSelectionForTest());
    CHECK(pane.surfaceForTest().highSideDb().size() == 16385);
    CHECK(pane.refusalMessageForTest().isEmpty());
}

TEST_CASE("xover axis: the same size at two sample rates is refused, both rates named",
          "[main_component_panes_xover_axis]") {
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::libraryForTest(component);
    const auto a = library.add(makePhaseTrace("at-48k", 4096, 48000.0), "A", "g");
    const auto b = library.add(makePhaseTrace("at-44k", 4096, 44100.0), "B", "g");

    auto& pane = pickPair(component, a, b);

    CHECK_FALSE(pane.hasCompleteSelectionForTest());
    const juce::String message = pane.refusalMessageForTest();
    CHECK(message.contains("48000 HZ"));
    CHECK(message.contains("44100 HZ"));
}
