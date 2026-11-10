// SPDX-License-Identifier: AGPL-3.0-or-later
// H10: the IR pane's MAG / PHASE / EXCESS lower plots and the tau0 readout, split from
// test_ir_pane_view.cpp at the 400-line cap. Fixture: ir_pane_view_rig.h.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ir_pane_view_rig.h"

using namespace irpane;
using Catch::Approx;
using Access = MainComponentTestAccess;

// ---- H10: MAG / PHASE / EXCESS ------------------------------------------------------

TEST_CASE("ir pane: MAG, PHASE and EXCESS draw their own curves, and tau0 joins the readout", kTag) {
    using rta::measure::IrLowerPlot;
    Rig rig(strongEchoIr());
    rig.pane->irComboForTest().setSelectedId(1, juce::sendNotificationSync);
    auto& gated = rig.pane->gatedPlotForTest();
    paintPane(rig);
    CHECK(gated.drawnDbForTest().size() > 100);
    CHECK(gated.drawnMeasuredDegForTest().empty());
    CHECK(Access::irForTest(rig.c).excessStartedForTest() == 0);  // MAG never asks for it

    rig.pane->lowerPlotButtonForTest(IrLowerPlot::Phase).onClick();
    paintPane(rig);
    CHECK(gated.emptyTextForTest() == "computing excess phase...");
    CHECK(gated.drawnMeasuredDegForTest().empty());
    REQUIRE(waitForExcess(rig));
    paintPane(rig);
    CHECK(gated.drawnDbForTest().empty());
    CHECK(gated.drawnMeasuredDegForTest().size() > 100);
    CHECK(gated.drawnMinimumDegForTest().size() == gated.drawnMeasuredDegForTest().size());
    expectTau0Shown(rig);
    const auto phaseCount = gated.drawnMeasuredDegForTest().size();

    rig.pane->lowerPlotButtonForTest(IrLowerPlot::Excess).onClick();
    paintPane(rig);
    CHECK(gated.drawnExcessDegForTest().size() > 100);
    CHECK(gated.drawnGroupDelayMsForTest().size() == gated.drawnExcessDegForTest().size());
    CHECK(gated.drawnMeasuredDegForTest().empty());
    // Over the trusted band only: the gate's fMin (104 Hz) is above the axis's 20 Hz edge,
    // so fewer bins than PHASE draws across the whole axis.
    CHECK(gated.drawnExcessDegForTest().size() < phaseCount);
    expectTau0Shown(rig);
    CHECK(rig.pane->readoutForTest().startsWith("GATE -1.0..10.0 ms"));

    rig.pane->lowerPlotButtonForTest(IrLowerPlot::Magnitude).onClick();
    paintPane(rig);
    CHECK(gated.drawnDbForTest().size() > 100);
    CHECK(gated.drawnExcessDegForTest().empty());
    CHECK_FALSE(rig.pane->readoutForTest().contains("tau0"));
    // Switching between the three never recomputed: one decomposition in all.
    CHECK(Access::irForTest(rig.c).excessStartedForTest() == 1);
}

TEST_CASE("ir pane: the decomposition never runs during a drag, only when the marker is released", kTag) {
    using rta::measure::IrLowerPlot;
    Rig rig(strongEchoIr());
    rig.pane->irComboForTest().setSelectedId(1, juce::sendNotificationSync);
    rig.pane->lowerPlotButtonForTest(IrLowerPlot::Excess).onClick();
    REQUIRE(waitForExcess(rig));
    auto& ir = Access::irForTest(rig.c);
    CHECK(ir.excessStartedForTest() == 1);

    auto& plot = rig.pane->etcPlotForTest();
    const auto end = plot.markerCentreForTest(false);
    const double pxPerMs = static_cast<double>(end.x - plot.markerCentreForTest(true).x) / 11.0;
    mouseAt(plot, end, true);
    for (int step = 1; step <= 12; ++step) {
        mouseAt(plot, {end.x + static_cast<int>(std::lround(0.5 * step * pxPerMs)), end.y}, false);
        rig.pane->tickForTest();
    }
    // Twelve marker moves later: not one decomposition began, and the stale one is gone.
    CHECK(ir.markersForTest().endMs > 14.0);
    CHECK(ir.excessStartedForTest() == 1);
    CHECK(ir.excessForTest() == nullptr);
    paintPane(rig);
    CHECK(rig.pane->gatedPlotForTest().emptyTextForTest() == "release the marker to compute excess phase");

    plot.mouseUp(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), end.toFloat(), juce::ModifierKeys(),
                                  1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &plot, &plot, juce::Time::getCurrentTime(),
                                  end.toFloat(), juce::Time::getCurrentTime(), 1, true));
    CHECK(ir.excessStartedForTest() == 2);
    REQUIRE(waitForExcess(rig));
    CHECK(ir.excessStartedForTest() == 2);
}

TEST_CASE("ir pane: a sweep that states no trusted band gets the kernel's refusal, not an empty curve", kTag) {
    using rta::measure::IrLowerPlot;
    Rig rig;  // echoIr(): trustedHighHz == 0
    rig.pane->irComboForTest().setSelectedId(1, juce::sendNotificationSync);
    rig.pane->lowerPlotButtonForTest(IrLowerPlot::Excess).onClick();
    REQUIRE(waitForExcess(rig));
    paintPane(rig);
    REQUIRE(Access::irForTest(rig.c).excessForTest() != nullptr);
    CHECK_FALSE(Access::irForTest(rig.c).excessForTest()->valid);
    CHECK(rig.pane->gatedPlotForTest().emptyTextForTest() == "too few trusted bins for excess phase");
    CHECK(rig.pane->gatedPlotForTest().drawnExcessDegForTest().empty());
    CHECK_FALSE(rig.pane->readoutForTest().contains("tau0"));
}

TEST_CASE("ir pane: the sweep's own low edge binds when a long gate reaches below it", kTag) {
    using rta::measure::IrLowerPlot;
    Rig rig(strongEchoIr());  // trustedLowHz = 50
    rig.pane->irComboForTest().setSelectedId(1, juce::sendNotificationSync);
    // -1..24 ms: fMin = 1 / (25 ms - 0.5 * 6.25 ms) = 45.7 Hz, BELOW the sweep's 50 Hz.
    Access::irForTest(rig.c).setMarkers({-1.0, 24.0});
    REQUIRE(Access::irForTest(rig.c).analysisForTest()->ok);
    REQUIRE(Access::irForTest(rig.c).analysisForTest()->fMinHz < 50.0);
    rig.pane->lowerPlotButtonForTest(IrLowerPlot::Excess).onClick();
    REQUIRE(waitForExcess(rig));
    const auto excess = Access::irForTest(rig.c).excessForTest();
    REQUIRE(excess->valid);
    CHECK(excess->trustedLowHz == Approx(50.0));  // the larger of the two floors
}
