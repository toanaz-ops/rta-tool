// SPDX-License-Identifier: AGPL-3.0-or-later
// L7-EQ UI wave A, tasks T4/T5 (docs/plans/2026-09-29-eq-ui-lane-plan.md): the
// fifth selector button and the EQ pane, driven through the REAL MainComponent
// and the pane's own widgets -- the same "drive the real control, not the
// model underneath it" shape test_main_component_panes_xover.cpp uses. The pane
// is reached through the existing MainComponentTestAccess::pane() plus a
// dynamic_cast; EqPaneView's `*ForTest` members are the only hooks (no EQ
// wrapper in MainComponentTestAccess -- the plan's orphan-check reasoning).
//
// Fixture: the plan's `makeLinearFixture` 8 dB bump at 1 kHz, as a stored
// TRANSFER-shaped trace (coherence 0.95). Bin 43 of 1025 (fftSize 2048,
// 48 kHz) is 43 * 23.4375 = 1007.8 Hz, the bin nearest the bump's centre.
#include <catch2/catch_test_macros.hpp>

#include "EqTraceFixture.h"
#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "view/EqChartRenderer.h"
#include "view/EqPaneView.h"
#include "view/RtaView.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using rta::view::EqPaneBinding;
using rta::view::EqPaneView;
using rta::view::PaneSelectorButton;
using rta::view::PaneView;

namespace {

constexpr std::size_t kBumpBin = 43;

juce::Button* findButtonByText(juce::Component& root, const juce::String& text) {
    for (int i = 0; i < root.getNumChildComponents(); ++i) {
        if (auto* button = dynamic_cast<juce::Button*>(root.getChildComponent(i))) {
            if (button->getButtonText() == text) return button;
        }
    }
    return nullptr;
}

EqPaneView& eqPane(MainComponent& component) {
    auto* pane = const_cast<EqPaneView*>(dynamic_cast<const EqPaneView*>(&MainComponentTestAccess::pane(component)));
    REQUIRE(pane != nullptr);
    return *pane;
}

/// Seeds the bump trace BEFORE the EQ pane exists, so its first setLibrary
/// populates the picker (a pane built earlier learns of library edits only on
/// its 10 Hz timer, which this offscreen harness never pumps).
std::string seedBump(MainComponent& component, const std::string& id = "bump") {
    return MainComponentTestAccess::libraryForTest(component).add(eqfixture::makeBumpTrace(id), "TRANSFER @ 12:00:00",
                                                                  "TRANSFER");
}

/// Measurement combo item 1 is the first eligible trace (`measurementIds_`).
void pickFirstMeasurement(EqPaneView& pane) {
    pane.measurementComboForTest().setSelectedId(1, juce::sendNotificationSync);
}

}  // namespace

TEST_CASE("the EQ button is the fifth selector cell and selects the EQ pane", "[main_component_panes_eq]") {
    MainComponent component;
    component.setSyntheticMode(true);

    auto* eqButton = findButtonByText(component, "EQ");
    REQUIRE(eqButton != nullptr);
    CHECK_FALSE(eqButton->getToggleState());

    component.selectPaneView(PaneSelectorButton::Eq);
    CHECK(component.currentPaneView() == PaneView::Eq);
    CHECK(eqButton->getToggleState());
    // Radio group: every other selector cell went dark.
    for (const char* other : { "RTA", "TRANSFER", "SPL", "XOVER" }) {
        auto* button = findButtonByText(component, other);
        REQUIRE(button != nullptr);
        CHECK_FALSE(button->getToggleState());
    }
    // Never an RtaView masquerading as the EQ pane.
    CHECK(dynamic_cast<const rta::view::RtaView*>(&MainComponentTestAccess::pane(component)) == nullptr);
    CHECK(dynamic_cast<const EqPaneView*>(&MainComponentTestAccess::pane(component)) != nullptr);
}

TEST_CASE("STORE is disabled on the EQ pane through both selection paths", "[main_component_panes_eq]") {
    MainComponent component;
    component.setSyntheticMode(true);
    REQUIRE(MainComponentTestAccess::storeButtonEnabledForTest(component));  // RTA at start

    component.selectPaneView(PaneSelectorButton::Eq);  // path 1: the selector click
    CHECK_FALSE(MainComponentTestAccess::storeButtonEnabledForTest(component));

    component.selectPaneView(PaneSelectorButton::Rta);
    REQUIRE(MainComponentTestAccess::storeButtonEnabledForTest(component));

    // path 2: session Open's rebuild. It must also light the EQ cell, like
    // the XOVER omission PR #45 round 3 fixed.
    MainComponentTestAccess::restoreWorkspaceForTest(component, { rta::trace::PaneSpec{ "eq" } });
    CHECK(component.currentPaneView() == PaneView::Eq);
    CHECK_FALSE(MainComponentTestAccess::storeButtonEnabledForTest(component));
    CHECK(findButtonByText(component, "EQ")->getToggleState());
    CHECK(dynamic_cast<const EqPaneView*>(&MainComponentTestAccess::pane(component)) != nullptr);

    // Save writes back the vocabulary Open reads.
    const auto specs = MainComponentTestAccess::paneSpecsForTest(component);
    REQUIRE(specs.size() == 1);
    CHECK(specs.front().view == "eq");
}

TEST_CASE("with an empty library the EQ pane refuses, naming STORE on TRANSFER with a reference",
          "[main_component_panes_eq]") {
    MainComponent component;
    component.setSyntheticMode(true);
    component.selectPaneView(PaneSelectorButton::Eq);
    auto& pane = eqPane(component);

    CHECK(pane.measurementIdsForTest().empty());
    CHECK(pane.refusalTextForTest().contains("STORE on TRANSFER with a reference"));
    CHECK_FALSE(pane.modelForTest().hasMeasurement());
    CHECK_FALSE(pane.autoEqButtonForTest().isEnabled());
    CHECK_FALSE(pane.suggestButtonForTest().isEnabled());
    // "Not asked": nothing is picked on the operator's behalf.
    CHECK(pane.measurementComboForTest().getSelectedId() == 0);
    CHECK(pane.firPhaseComboForTest().getSelectedId() == 0);
    CHECK(pane.firTapsComboForTest().getSelectedId() == 0);
}

TEST_CASE("AUTO EQ through the real combo and the real button lands filters nearer the target",
          "[main_component_panes_eq]") {
    MainComponent component;
    component.setSyntheticMode(true);
    seedBump(component);
    component.selectPaneView(PaneSelectorButton::Eq);
    auto& pane = eqPane(component);
    REQUIRE(pane.measurementIdsForTest().size() == 1);
    CHECK(pane.measurementComboForTest().getSelectedId() == 0);  // starts "not asked" even with one candidate

    pickFirstMeasurement(pane);
    auto& model = pane.modelForTest();
    REQUIRE(model.hasMeasurement());
    CHECK(pane.autoEqButtonForTest().isEnabled());

    pane.autoEqButtonForTest().onClick();

    REQUIRE_FALSE(model.session().committed().empty());
    // The invariant of test_eq_session.cpp ("Auto EQ leaves the ghost closer
    // to target than the measurement"), reached through the button.
    const double target = static_cast<double>(model.targetDbForTest()[kBumpBin]);
    const double measured = static_cast<double>(model.measuredDb()[kBumpBin]);
    const double ghost = model.session().ghostDb()[kBumpBin];
    CHECK(std::abs(ghost - target) < std::abs(measured - target));
    CHECK(pane.filterPanelForTest().visibleRowCountForTest() == model.session().committed().size());
    CHECK(pane.readoutForTest().contains("AUTO EQ:"));
}

TEST_CASE("BANDS caps what AUTO EQ commits, through the combo", "[main_component_panes_eq]") {
    MainComponent component;
    component.setSyntheticMode(true);
    seedBump(component);
    component.selectPaneView(PaneSelectorButton::Eq);
    auto& pane = eqPane(component);
    pickFirstMeasurement(pane);

    CHECK(pane.bandsComboForTest().getSelectedId() == 6);  // D4's default
    pane.bandsComboForTest().setSelectedId(1, juce::sendNotificationSync);
    pane.autoEqButtonForTest().onClick();
    CHECK(pane.modelForTest().session().committed().size() == 1);
}

TEST_CASE("SUGGEST shows min(3, ranked) chips; DECLINE removes its band from the next SUGGEST",
          "[main_component_panes_eq]") {
    MainComponent component;
    component.setSyntheticMode(true);
    seedBump(component);
    component.selectPaneView(PaneSelectorButton::Eq);
    auto& pane = eqPane(component);
    pickFirstMeasurement(pane);
    auto& model = pane.modelForTest();

    const std::size_t ranked = model.session().suggest(16).size();
    REQUIRE(ranked >= 1);
    pane.suggestButtonForTest().onClick();
    auto& panel = pane.filterPanelForTest();
    CHECK(panel.visibleChipCountForTest() == std::min<std::size_t>(3, ranked));
    CHECK(model.session().committed().empty());  // SUGGEST commits nothing

    const double declinedFc = model.chips().front().spec.fcHz;
    panel.declineButtonForTest(0).onClick();
    CHECK(model.session().committed().empty());  // DECLINE commits nothing either

    pane.suggestButtonForTest().onClick();
    for (const auto& chip : model.chips()) CHECK(chip.spec.fcHz != declinedFc);

    SECTION("ACCEPT commits the chip and the ranking re-bases") {
        REQUIRE_FALSE(model.chips().empty());
        const double acceptedFc = model.chips().front().spec.fcHz;
        panel.acceptButtonForTest(0).onClick();
        REQUIRE(model.session().committed().size() == 1);
        CHECK(model.session().committed().front().spec.fcHz == acceptedFc);
        CHECK(panel.visibleRowCountForTest() == 1);
    }
}

TEST_CASE("switching to TRANSFER and back leaves the committed list intact", "[main_component_panes_eq]") {
    MainComponent component;
    component.setSyntheticMode(true);
    seedBump(component);
    component.selectPaneView(PaneSelectorButton::Eq);
    pickFirstMeasurement(eqPane(component));
    eqPane(component).autoEqButtonForTest().onClick();
    const auto before = eqPane(component).modelForTest().session().committed().size();
    REQUIRE(before >= 1);
    const double firstFc = eqPane(component).modelForTest().session().committed().front().spec.fcHz;

    component.selectPaneView(PaneSelectorButton::Transfer);  // destroys the EQ pane
    CHECK(dynamic_cast<const EqPaneView*>(&MainComponentTestAccess::pane(component)) == nullptr);
    component.selectPaneView(PaneSelectorButton::Eq);         // builds a new one

    auto& pane = eqPane(component);
    const auto& committed = pane.modelForTest().session().committed();
    REQUIRE(committed.size() == before);
    CHECK(committed.front().spec.fcHz == firstFc);
    // The rebuilt pane draws the surviving state, and still names the pick.
    CHECK(pane.filterPanelForTest().visibleRowCountForTest() == before);
    CHECK(pane.modelForTest().hasMeasurement());
    CHECK(pane.measurementComboForTest().getSelectedId() == 1);
}

TEST_CASE("EXPORT FIR stays disabled until BOTH the phase and the length are answered (D8, D9)",
          "[main_component_panes_eq]") {
    MainComponent component;
    component.setSyntheticMode(true);
    seedBump(component);
    component.selectPaneView(PaneSelectorButton::Eq);
    auto& pane = eqPane(component);
    pickFirstMeasurement(pane);
    pane.autoEqButtonForTest().onClick();
    REQUIRE_FALSE(pane.modelForTest().session().committed().empty());

    // Filters exist and a measurement is picked, yet nothing is asked.
    CHECK_FALSE(pane.modelForTest().firPhase().has_value());
    CHECK_FALSE(pane.modelForTest().firTaps().has_value());
    CHECK_FALSE(pane.exportFirTextButtonForTest().isEnabled());
    CHECK_FALSE(pane.exportFirWavButtonForTest().isEnabled());
    CHECK(pane.exportListButtonForTest().isEnabled());  // a list needs no FIR questions

    // Answer only the length (the pre-selected-phase mutant fails HERE).
    pane.firTapsComboForTest().setSelectedId(2, juce::sendNotificationSync);
    REQUIRE(pane.modelForTest().firTaps().has_value());
    CHECK_FALSE(pane.exportFirTextButtonForTest().isEnabled());
    CHECK_FALSE(pane.exportFirWavButtonForTest().isEnabled());

    pane.firPhaseComboForTest().setSelectedId(1, juce::sendNotificationSync);  // LINEAR
    CHECK(pane.exportFirTextButtonForTest().isEnabled());
    CHECK(pane.exportFirWavButtonForTest().isEnabled());

    // The picker prints fs/N and (N-1)/(2fs) beside each length (D9), at the
    // measurement's own rate: 48000/4095 = 11.7 -> "12 Hz"; 4094/96000 = 42.6 ms.
    CHECK(pane.firTapsComboForTest().getItemText(1) == "4095 taps  12 Hz  42.6 ms");
}

TEST_CASE("the export buttons call the bound actions, and only when enabled", "[main_component_panes_eq]") {
    rta::trace::TraceLibrary library;
    library.add(eqfixture::makeBumpTrace("bump"), "TRANSFER @ 12:00:00", "TRANSFER");
    rta::measure::EqPaneModel model;
    int text = 0, wav = 0, list = 0;
    EqPaneBinding binding{ &model, { [&] { ++text; }, [&] { ++wav; }, [&] { ++list; } } };
    EqPaneView pane(binding);
    pane.setLibrary(&library);

    pickFirstMeasurement(pane);
    pane.autoEqButtonForTest().onClick();
    pane.firPhaseComboForTest().setSelectedId(2, juce::sendNotificationSync);  // MINIMUM
    pane.firTapsComboForTest().setSelectedId(1, juce::sendNotificationSync);   // 1023
    REQUIRE(pane.exportFirTextButtonForTest().isEnabled());

    pane.exportFirTextButtonForTest().onClick();
    pane.exportFirWavButtonForTest().onClick();
    pane.exportListButtonForTest().onClick();
    CHECK(text == 1);
    CHECK(wav == 1);
    CHECK(list == 1);
}

TEST_CASE("a picked trace that leaves the library stays named, and the pane keeps its numbers",
          "[main_component_panes_eq]") {
    rta::trace::TraceLibrary library;
    const auto id = library.add(eqfixture::makeBumpTrace("bump"), "TRANSFER @ 12:00:00", "TRANSFER");
    rta::measure::EqPaneModel model;
    EqPaneView pane(EqPaneBinding{ &model, {} });
    pane.setLibrary(&library);
    pickFirstMeasurement(pane);
    pane.autoEqButtonForTest().onClick();
    const auto filters = model.session().committed().size();
    REQUIRE(filters >= 1);

    REQUIRE(library.setVisible(id, false));
    pane.setLibrary(&library);  // what the pane's own timer does on a library revision

    CHECK(model.measurement().removed);
    CHECK(model.hasMeasurement());
    CHECK(model.session().committed().size() == filters);
    CHECK(pane.measurementComboForTest().getText().contains("(removed)"));
}

TEST_CASE("the chart's dB range follows the trusted bins, not the noise beneath the coherence floor",
          "[main_component_panes_eq]") {
    // A SYNTHETIC transfer capture reads +130 dB in bins where the reference
    // has no energy (found rendering main-live-store-eq.png). Those bins are
    // untrusted, and must not set the axis the trusted 0 dB curve is drawn on.
    const std::vector<float> hz{ 50.0f, 100.0f, 1000.0f, 10000.0f };
    const std::vector<float> measured{ 120.0f, 0.0f, 2.0f, -1.0f };
    const std::vector<double> same{ 120.0, 0.0, 2.0, -1.0 };
    const std::vector<double> flat{ 0.0, 0.0, 0.0, 0.0 };
    const std::vector<std::uint8_t> trusted{ 0, 1, 1, 1 };
    rta::view::EqChartData data;
    data.hz = hz;
    data.measuredDb = measured;
    data.ghostDb = same;
    data.targetLineDb = flat;
    data.trusted = trusted;

    // Trusted values span -1..2 dB: pad 3 dB and round out to 10 -> [-10, 10],
    // then widen to the 30 dB minimum span by raising the top: [-10, 20].
    auto range = rta::view::eqChartDbRange(data);
    CHECK(range.top == 20.0);
    CHECK(range.bottom == -10.0);

    // Nothing trusted: every bin counts. -1..120 dB -> [-10, 130].
    const std::vector<std::uint8_t> none{ 0, 0, 0, 0 };
    data.trusted = none;
    range = rta::view::eqChartDbRange(data);
    CHECK(range.top == 130.0);
    CHECK(range.bottom == -10.0);

    // The correction strip: the smallest multiple of 10 dB (>= 10) holding max|R|.
    CHECK(rta::view::eqStripHalfRangeDb(std::vector<double>{ 0.0, -13.0, 4.0 }) == 20.0);
    CHECK(rta::view::eqStripHalfRangeDb(std::vector<double>{ 0.0, 9.9 }) == 10.0);
    CHECK(rta::view::eqStripHalfRangeDb(std::vector<double>{}) == 10.0);
}
