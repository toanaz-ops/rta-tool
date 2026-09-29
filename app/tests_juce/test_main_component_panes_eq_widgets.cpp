// SPDX-License-Identifier: AGPL-3.0-or-later
// L7-EQ UI wave A, task T5 (docs/plans/2026-09-29-eq-ui-lane-plan.md): the
// remaining EQ pane widgets -- the target picker, the applied ticks, the chip
// text, the normalise toggle and the painted chart -- against a standalone
// EqPaneView (no MainComponent needed for these). Split from
// test_main_component_panes_eq.cpp, which is near the 400-line cap.
#include <catch2/catch_test_macros.hpp>

#include "EqTraceFixture.h"
#include "export/FirExport.h"
#include "view/EqPaneView.h"

#include <cmath>
#include <regex>
#include <string>
#include <vector>

using rta::view::EqPaneBinding;
using rta::view::EqPaneView;

namespace {

/// A standalone pane over a library holding the 8 dB bump (id "bump") and,
/// optionally, a second stored trace to use as a target.
struct PaneRig {
    rta::trace::TraceLibrary library;
    rta::measure::EqPaneModel model;
    EqPaneView pane{ EqPaneBinding{ &model, {} } };
    PaneRig() {
        library.add(eqfixture::makeBumpTrace("bump"), "TRANSFER @ 12:00:00", "TRANSFER");
        pane.setLibrary(&library);
    }
    void pickAndAutoEq() {
        pane.measurementComboForTest().setSelectedId(1, juce::sendNotificationSync);
        pane.autoEqButtonForTest().onClick();
    }
};

}  // namespace

TEST_CASE("the target combo starts on FLAT and a stored trace becomes the target", "[main_component_panes_eq]") {
    PaneRig rig;
    // A second stored trace on the SAME grid, a shaped curve to aim at.
    std::vector<float> shaped(1025);
    for (std::size_t k = 0; k < shaped.size(); ++k) shaped[k] = 0.01f * static_cast<float>(k) - 3.0f;
    rig.library.add(eqfixture::makeEqTrace("shape", 2048, eqfixture::kFs, shaped), "Shaped", "default");
    rig.pane.setLibrary(&rig.library);  // what the pane's own timer does on a library revision

    auto& targetCombo = rig.pane.targetComboForTest();
    CHECK(targetCombo.getSelectedId() == 1);  // FLAT: D3's default, unlike the measurement picker
    CHECK(targetCombo.getNumItems() == 3);    // FLAT + both stored traces (a target needs no coherence)

    rig.pane.measurementComboForTest().setSelectedId(1, juce::sendNotificationSync);
    REQUIRE(rig.model.hasMeasurement());
    CHECK(rig.model.target().id.empty());

    targetCombo.setSelectedId(3, juce::sendNotificationSync);  // item 3 = the second library entry
    CHECK(rig.model.target().name == "Shaped");
    // Same grid: the target is the stored curve, copied bitwise.
    REQUIRE(rig.model.targetDbForTest().size() == shaped.size());
    CHECK(std::equal(shaped.begin(), shaped.end(), rig.model.targetDbForTest().begin()));
    CHECK(rig.pane.readoutForTest().contains("TARGET Shaped"));

    targetCombo.setSelectedId(1, juce::sendNotificationSync);  // back to FLAT
    CHECK(rig.model.target().id.empty());
    CHECK(rig.pane.readoutForTest().contains("TARGET FLAT"));
}

TEST_CASE("ticking a committed row marks it applied: the ghost then IS the measurement", "[main_component_panes_eq]") {
    PaneRig rig;
    rig.pickAndAutoEq();
    const auto n = rig.model.session().committed().size();
    REQUIRE(n >= 1);
    auto& panel = rig.pane.filterPanelForTest();
    REQUIRE(panel.visibleRowCountForTest() == n);

    for (std::size_t i = 0; i < n; ++i) {
        auto& tick = panel.appliedToggleForTest(i);
        CHECK_FALSE(tick.getToggleState());
        tick.setToggleState(true, juce::dontSendNotification);
        tick.onClick();  // the real handler
    }
    for (const auto& f : rig.model.session().committed()) CHECK(f.applied);

    // EqSession's one membership rule: an applied filter is already in the
    // measurement, so with EVERY filter applied nothing is added to it.
    const auto ghost = rig.model.session().ghostDb();
    const auto measured = rig.model.measuredDb();
    REQUIRE(ghost.size() == measured.size());
    bool identical = true;
    for (std::size_t k = 0; k < ghost.size(); ++k) identical = identical && (ghost[k] == static_cast<double>(measured[k]));
    CHECK(identical);
    // Nothing left to realise, so no FIR export; the tick survives a redraw.
    CHECK_FALSE(rig.model.canExportFir());
    panel.refresh(rig.model);
    CHECK(panel.appliedToggleForTest(0).getToggleState());
}

TEST_CASE("chip text reads whole hertz, Q to two decimals, dB to one", "[main_component_panes_eq]") {
    PaneRig rig;
    rig.pane.measurementComboForTest().setSelectedId(1, juce::sendNotificationSync);
    rig.pane.suggestButtonForTest().onClick();
    auto& panel = rig.pane.filterPanelForTest();
    REQUIRE(panel.visibleChipCountForTest() >= 1);

    // CLAUDE.md "Reading out numbers": 1000 Hz not 1000.0 Hz; dB keeps one
    // decimal; Q is two (EqTextExport's own precision).
    const std::regex shape(R"(^1  peaking [0-9]+ Hz  Q [0-9]+\.[0-9]{2}  [+-][0-9]+\.[0-9] dB$)");
    const std::string text = panel.chipTextForTest(0).toStdString();
    INFO(text);
    CHECK(std::regex_match(text, shape));
    CHECK(text == "1  " + rta::view::eqFilterText(rig.model.chips().front().spec));
}

TEST_CASE("the normalise toggle switches the FIR normalisation, as_designed by default (D12)",
          "[main_component_panes_eq]") {
    PaneRig rig;
    CHECK(rig.model.normalization() == rta::firexport::Normalization::AsDesigned);
    CHECK_FALSE(rig.pane.peakToggleForTest().getToggleState());

    rig.pane.peakToggleForTest().setToggleState(true, juce::dontSendNotification);
    rig.pane.peakToggleForTest().onClick();
    CHECK(rig.model.normalization() == rta::firexport::Normalization::Peak0dBFS);
    rig.pane.peakToggleForTest().setToggleState(false, juce::dontSendNotification);
    rig.pane.peakToggleForTest().onClick();
    CHECK(rig.model.normalization() == rta::firexport::Normalization::AsDesigned);
}

TEST_CASE("the chart is painted only once a measurement is picked", "[main_component_panes_eq]") {
    PaneRig rig;
    constexpr int kWidth = 900;
    constexpr int kHeight = 600;
    rig.pane.setSize(kWidth, kHeight);

    const auto render = [&] {
        juce::Image image(juce::Image::ARGB, kWidth, kHeight, true);
        juce::Graphics g(image);
        rig.pane.paintEntireComponent(g, true);
        return image;
    };
    // Amber is the measured trace's colour (`trace`, az::ui::accent). Count
    // pixels within reach of it, right of the controls column.
    const auto amberPixels = [&](const juce::Image& image) {
        int count = 0;
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 330; x < kWidth; ++x) {
                const auto c = image.getPixelAt(x, y);
                if (c.getRed() > 200 && c.getGreen() > 110 && c.getGreen() < 200 && c.getBlue() < 90) ++count;
            }
        }
        return count;
    };

    const auto before = render();
    CHECK_FALSE(rig.pane.lastPaintDrewChartForTest());  // the refusal text, not a chart
    CHECK(amberPixels(before) == 0);

    rig.pickAndAutoEq();
    const auto after = render();
    CHECK(rig.pane.lastPaintDrewChartForTest());
    CHECK(amberPixels(after) > 100);  // the 8 dB bump alone is hundreds of pixels of amber
}

TEST_CASE("CLEAR drops the committed filters; UNDECLINE lets a declined band be suggested again",
          "[main_component_panes_eq]") {
    PaneRig rig;
    rig.pane.measurementComboForTest().setSelectedId(1, juce::sendNotificationSync);
    rig.pane.suggestButtonForTest().onClick();
    REQUIRE_FALSE(rig.model.chips().empty());
    const double topFc = rig.model.chips().front().spec.fcHz;

    rig.pane.filterPanelForTest().declineButtonForTest(0).onClick();
    for (const auto& chip : rig.model.chips()) CHECK(chip.spec.fcHz != topFc);  // declined: gone

    rig.pane.undeclineButtonForTest().onClick();  // "I changed my mind about the regions I refused"
    rig.pane.suggestButtonForTest().onClick();
    REQUIRE_FALSE(rig.model.chips().empty());
    CHECK(rig.model.chips().front().spec.fcHz == topFc);  // the same greedy first move, back

    rig.pane.autoEqButtonForTest().onClick();
    REQUIRE_FALSE(rig.model.session().committed().empty());
    CHECK(rig.pane.clearButtonForTest().isEnabled());
    rig.pane.clearButtonForTest().onClick();
    CHECK(rig.model.session().committed().empty());
    CHECK(rig.pane.filterPanelForTest().visibleRowCountForTest() == 0);
    CHECK_FALSE(rig.pane.clearButtonForTest().isEnabled());
}
