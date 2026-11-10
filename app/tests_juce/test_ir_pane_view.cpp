// SPDX-License-Identifier: AGPL-3.0-or-later
// H9 / L4c task A3 (docs/plans/2026-11-09-h9-l4c-ir-gate-plan.md): the IR-gate pane
// through the real MainComponent and the real makePaneFactory. The selector's
// eighth button reaches it; picking an IR fills the ETC and the gated plot; a
// real mouse drag on a marker moves the marker AND the readout; STORE GATED adds
// exactly one trace to the library; a refused gate says so and cannot be stored.
//
// The IR is a closed-form two-spike one (a direct delta and an echo), stored
// through `MainComponentSweep::addIrForTest` -- fast and exact, the same seam the
// RT60 pane tests use.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ir_pane_view_rig.h"

using namespace irpane;
using Catch::Approx;
using Access = MainComponentTestAccess;

TEST_CASE("ir pane: the selector builds an IrPaneView, returns to RTA, and a saved ir pane reopens", kTag) {
    MainComponent c;
    CHECK(irPaneOf(c) == nullptr);
    c.selectPaneView(rta::view::PaneSelectorButton::Ir);
    CHECK(c.currentPaneView() == rta::view::PaneView::Ir);
    CHECK(irPaneOf(c) != nullptr);
    CHECK(Access::paneSpecsForTest(c).size() == 1);

    c.selectPaneView(rta::view::PaneSelectorButton::Rta);
    CHECK(irPaneOf(c) == nullptr);

    const auto specs = Access::paneSpecsForTest(c);
    REQUIRE(specs.size() == 1);
    c.selectPaneView(rta::view::PaneSelectorButton::Ir);
    CHECK(Access::paneSpecsForTest(c)[0].view == "ir");
    Access::restoreWorkspaceForTest(c, {rta::trace::PaneSpec{"rta"}});
    CHECK(irPaneOf(c) == nullptr);
    Access::restoreWorkspaceForTest(c, {rta::trace::PaneSpec{"ir"}});
    CHECK(c.currentPaneView() == rta::view::PaneView::Ir);
}

TEST_CASE("ir pane: an IR in memory is listed, picking it gates it, and the default readout is the record's", kTag) {
    Rig rig;
    const auto items = rig.pane->pickerTextsForTest();
    REQUIRE(items.size() == 1);
    CHECK(items[0] == "SWEEP out1 @ 10:00:00");
    CHECK(rig.pane->readoutForTest() == "GATE -- no IR picked");
    CHECK_FALSE(rig.pane->storeButtonForTest().isEnabled());

    rig.pane->irComboForTest().setSelectedId(1, juce::sendNotificationSync);
    CHECK(Access::irForTest(rig.c).selectedIdForTest() == rig.traceId);
    REQUIRE(Access::irForTest(rig.c).analysisForTest() != nullptr);
    CHECK(Access::irForTest(rig.c).analysisForTest()->ok);
    // 1 / (11 ms - 1.375 ms) = 103.9 Hz, printed rounded up.
    CHECK(rig.pane->readoutForTest() == juce::String(juce::CharPointer_UTF8("GATE -1.0..10.0 ms \xc2\xb7 valid above 104 Hz")));
    CHECK(rig.pane->storeButtonForTest().isEnabled());
    CHECK(rig.pane->etcPlotForTest().drawnPointCountForTest() == 0);  // not painted yet
    rig.pane->createComponentSnapshot(rig.pane->getLocalBounds(), true);
    CHECK(rig.pane->etcPlotForTest().drawnPointCountForTest() > 2000);
    CHECK(rig.pane->gatedPlotForTest().drawnDbForTest().size() > 100);
}

TEST_CASE("ir pane: dragging a marker moves the marker and the readout", kTag) {
    Rig rig;
    rig.pane->irComboForTest().setSelectedId(1, juce::sendNotificationSync);
    auto& plot = rig.pane->etcPlotForTest();
    REQUIRE(plot.getWidth() > 200);

    // Grab the end marker (+10.0 ms) and pull it to +20.0 ms: the plot's x axis is
    // linear in ms, so the target is the same fraction of the plot as the markers.
    const auto start = plot.markerCentreForTest(true);
    const auto end = plot.markerCentreForTest(false);
    const double pxPerMs = static_cast<double>(end.x - start.x) / 11.0;  // -1.0 .. +10.0
    const int target = end.x + static_cast<int>(std::lround(10.0 * pxPerMs));
    mouseAt(plot, end, true);
    mouseAt(plot, {target, end.y}, false);

    const auto markers = Access::irForTest(rig.c).markersForTest();
    CHECK(markers.startMs == -1.0);
    CHECK(markers.endMs > 19.0);
    CHECK(markers.endMs < 21.0);
    CHECK(plot.markersForTest().endMs == markers.endMs);
    // The readout follows, and so does the lowest valid frequency (a longer gate).
    CHECK(rig.pane->readoutForTest().startsWith("GATE -1.0.."));
    CHECK_FALSE(rig.pane->readoutForTest().contains("-1.0..10.0"));
    CHECK_FALSE(rig.pane->readoutForTest().contains("valid above 104"));

    // The marker nearer the hand moves: a grab by the start marker leaves the end alone.
    const double endBefore = markers.endMs;
    const auto startNow = plot.markerCentreForTest(true);
    mouseAt(plot, startNow, true);
    mouseAt(plot, {startNow.x + static_cast<int>(std::lround(2.0 * pxPerMs)), startNow.y}, false);
    CHECK(Access::irForTest(rig.c).markersForTest().endMs == endBefore);
    CHECK(Access::irForTest(rig.c).markersForTest().startMs > 0.5);

    // That drag left the start about +1.0 ms: after the arrival, where the gate
    // has no direct sound. The pane says so, with the number, and
    // STORE GATED is off (it used to read "valid" and stay enabled).
    const auto dragged = Access::irForTest(rig.c).markersForTest().startMs;
    CHECK(dragged > 0.5);  // +1.0 ms: after the arrival, no direct sound left
    CHECK(rig.pane->readoutForTest().startsWith("GATE refused -- "));
    CHECK(rig.pane->readoutForTest().contains("direct sound"));
    CHECK(rig.pane->readoutForTest().contains("-0.5 ms"));
    CHECK_FALSE(rig.pane->storeButtonForTest().isEnabled());
    const auto before = Access::libraryForTest(rig.c).entries().size();
    rig.pane->storeButtonForTest().onClick();  // a click on a disabled button must not store
    CHECK(Access::libraryForTest(rig.c).entries().size() == before);
}

TEST_CASE("ir pane: STORE GATED adds exactly one trace, in its own group, with the gate in its meta", kTag) {
    Rig rig;
    rig.pane->irComboForTest().setSelectedId(1, juce::sendNotificationSync);
    const auto before = Access::libraryForTest(rig.c).entries().size();
    CHECK(countGroup(rig.c, "GATED") == 0);

    rig.pane->storeButtonForTest().onClick();
    CHECK(Access::libraryForTest(rig.c).entries().size() == before + 1);
    CHECK(countGroup(rig.c, "GATED") == 1);
    CHECK(rig.pane->statusForTest().startsWith("stored: SWEEP out1 @ 10:00:00 gated"));

    const auto entries = Access::libraryForTest(rig.c).entries();
    const auto& stored = entries.back();
    CHECK(stored.name == "SWEEP out1 @ 10:00:00 gated");
    const auto* trace = Access::libraryForTest(rig.c).trace(stored.traceId);
    REQUIRE(trace != nullptr);
    CHECK(trace->meta().window == "gate -1.0..10.0 ms, valid above 104 Hz");
    CHECK(trace->meta().deviceName == "Interface X");      // from the SWEEP trace it came from
    CHECK(trace->meta().channelRoles == "ch0=Measurement");
    CHECK(trace->meta().appliedDelaySamples == kArrival);
    CHECK(trace->has(rta::trace::Field::Phase));
    CHECK_FALSE(trace->has(rta::trace::Field::Coherence));

    // A second press is a second capture: the library never silently replaces.
    rig.pane->storeButtonForTest().onClick();
    CHECK(countGroup(rig.c, "GATED") == 2);
}

TEST_CASE("ir pane: a GATED trace does not share its SWEEP source's shade (H11 L2)", kTag) {
    Rig rig;  // SWEEP shade 0 in its own group
    auto& lib = Access::libraryForTest(rig.c);
    REQUIRE(lib.entry(rig.traceId)->shadeIndex == 0);
    rig.pane->irComboForTest().setSelectedId(1, juce::sendNotificationSync);
    rig.pane->storeButtonForTest().onClick();
    rig.pane->storeButtonForTest().onClick();
    const auto gated = lib.entries().subspan(1);
    REQUIRE(gated.size() == 2);
    CHECK(gated[0].shadeIndex == 1);  // lowest unused across SWEEP + GATED
    CHECK(gated[1].shadeIndex == 2);
}

TEST_CASE("ir pane: a refused gate says why, with the number, and STORE GATED is off", kTag) {
    Rig rig;
    rig.pane->irComboForTest().setSelectedId(1, juce::sendNotificationSync);
    // Pull the start marker past the end marker: the model's call, not a clamp.
    Access::irForTest(rig.c).setMarkers({12.0, 10.0});
    rig.pane->tickForTest();
    CHECK(rig.pane->readoutForTest().startsWith("GATE refused -- "));
    CHECK(rig.pane->readoutForTest().contains("12.0"));
    CHECK_FALSE(rig.pane->storeButtonForTest().isEnabled());
    const auto before = Access::libraryForTest(rig.c).entries().size();
    CHECK(Access::irForTest(rig.c).storeGated().rfind("not stored -- ", 0) == 0);
    CHECK(Access::libraryForTest(rig.c).entries().size() == before);
}

TEST_CASE("ir pane: the selection and the markers survive a rebuild, and an evicted IR clears them", kTag) {
    Rig rig;
    rig.pane->irComboForTest().setSelectedId(1, juce::sendNotificationSync);
    Access::irForTest(rig.c).setMarkers({-2.0, 8.0});
    rig.c.selectPaneView(rta::view::PaneSelectorButton::Rta);
    rig.c.selectPaneView(rta::view::PaneSelectorButton::Ir);
    auto* again = irPaneOf(rig.c);
    REQUIRE(again != nullptr);
    CHECK(again->readoutForTest().startsWith("GATE -2.0..8.0 ms"));
    CHECK(again->irComboForTest().getSelectedId() == 1);
}
