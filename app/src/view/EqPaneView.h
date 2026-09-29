// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view.
//
// L7-EQ UI task T5 (docs/plans/2026-09-29-eq-ui-lane-plan.md): the fifth pane.
// Controls on the left -- measurement and target pickers, BANDS, AUTO EQ,
// SUGGEST with its ACCEPT / DECLINE chips, the committed list, the FIR phase
// and length questions, the exports -- and on the right the chart
// (EqChartRenderer). The pane owns WIDGETS ONLY. Every operator choice and
// every number lives in the EqPaneModel MainComponentEq owns, so switching to
// TRANSFER and back (which rebuilds the pane, MainComponentPanes.cpp) loses
// nothing.
//
// D2/D8/D9: the measurement picker starts "not asked", and the FIR phase and
// length combos start "not asked" too -- nothing is pre-selected on the
// operator's behalf, and EXPORT FIR stays disabled until both are answered.
#pragma once

#include "measure/EqPaneModel.h"
#include "view/EqFilterPanel.h"
#include "view/PaneRegistry.h"
#include "view/RepaintGate.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rta::view {

/// What EXPORT does. Provided by MainComponentEq (file dialogs and writes are
/// the composition root's business, not the pane's).
struct EqPaneActions {
    std::function<void()> exportFirText;
    std::function<void()> exportFirWav;
    std::function<void()> exportList;
};

/// The pane's whole wiring, handed to `makePaneFactory`. `model` is borrowed
/// from MainComponentEq and must outlive every pane built from this.
struct EqPaneBinding {
    rta::measure::EqPaneModel* model = nullptr;
    EqPaneActions actions;
};

class EqPaneView final : public juce::Component, public LibraryConsumer, private juce::Timer {
public:
    explicit EqPaneView(EqPaneBinding binding);
    ~EqPaneView() override;

    void setLibrary(const rta::trace::TraceLibrary* library) override;
    void paint(juce::Graphics&) override;
    void resized() override;

    // --- test hooks: every name ends in ForTest and is referenced whole-word
    // from app/tests_juce/test_main_component_panes_eq*.cpp (orphan_check's
    // TEST HOOK category). No MainComponentTestAccess wrapper exists for EQ:
    // tests reach this class through the existing pane() plus a dynamic_cast.
    [[nodiscard]] rta::measure::EqPaneModel& modelForTest() noexcept { return model_; }
    [[nodiscard]] juce::ComboBox& measurementComboForTest() noexcept { return measurementCombo_; }
    [[nodiscard]] juce::ComboBox& targetComboForTest() noexcept { return targetCombo_; }
    [[nodiscard]] juce::ComboBox& bandsComboForTest() noexcept { return bandsCombo_; }
    [[nodiscard]] juce::ComboBox& firPhaseComboForTest() noexcept { return firPhaseCombo_; }
    [[nodiscard]] juce::ComboBox& firTapsComboForTest() noexcept { return firTapsCombo_; }
    [[nodiscard]] juce::Button& autoEqButtonForTest() noexcept { return autoEqButton_; }
    [[nodiscard]] juce::Button& suggestButtonForTest() noexcept { return suggestButton_; }
    [[nodiscard]] juce::Button& exportFirTextButtonForTest() noexcept { return exportFirTextButton_; }
    [[nodiscard]] juce::Button& exportFirWavButtonForTest() noexcept { return exportFirWavButton_; }
    [[nodiscard]] juce::Button& exportListButtonForTest() noexcept { return exportListButton_; }
    [[nodiscard]] juce::Button& peakToggleForTest() noexcept { return peakToggle_; }
    [[nodiscard]] juce::Button& clearButtonForTest() noexcept { return clearButton_; }
    [[nodiscard]] juce::Button& undeclineButtonForTest() noexcept { return undeclineButton_; }
    [[nodiscard]] EqFilterPanel& filterPanelForTest() noexcept { return filterPanel_; }
    [[nodiscard]] const std::vector<std::string>& measurementIdsForTest() const noexcept { return measurementIds_; }
    [[nodiscard]] juce::String readoutForTest() const { return readout_.getText(); }
    [[nodiscard]] juce::String refusalTextForTest() const { return refusalMessage(); }
    [[nodiscard]] bool lastPaintDrewChartForTest() const noexcept { return lastPaintDrewChart_; }

private:
    void timerCallback() override;
    /// Rebuilds the measurement / target combos from the library and the
    /// model's picks (a pick that left the library stays listed, "(removed)").
    void rebuildPickers();
    /// Everything that follows the model: combos' selections, enable states,
    /// chips, list, readout, chart cache. One function, so the widgets can
    /// never disagree with the model about what the pane shows.
    void refreshFromModel();
    void measurementChanged();
    void targetChanged();
    [[nodiscard]] juce::String refusalMessage() const;

    EqPaneBinding binding_;
    rta::measure::EqPaneModel& model_;
    const rta::trace::TraceLibrary* library_ = nullptr;
    GateState gate_;
    std::uint64_t lastModelRevision_ = ~std::uint64_t{ 0 };

    std::vector<std::string> measurementIds_;  ///< combo item id i+1 names [i]
    std::vector<std::string> targetIds_;       ///< combo item id i+2 names [i]; id 1 is FLAT

    // Chart cache: recomputed only when the model's revision moves.
    std::vector<float> hz_;
    std::vector<double> ghostDb_, targetLineDb_, correctionDb_;
    bool lastPaintDrewChart_ = false;

    juce::ComboBox measurementCombo_, targetCombo_, bandsCombo_, firPhaseCombo_, firTapsCombo_;
    juce::TextButton autoEqButton_{ "AUTO EQ" }, suggestButton_{ "SUGGEST" };
    juce::TextButton clearButton_{ "CLEAR" }, undeclineButton_{ "UNDECLINE" };
    juce::ToggleButton peakToggle_{ "NORMALISE TO 0 dBFS" };
    juce::TextButton exportFirTextButton_{ "EXPORT FIR TXT" }, exportFirWavButton_{ "EXPORT FIR WAV" };
    juce::TextButton exportListButton_{ "EXPORT LIST" };
    juce::Label readout_;
    EqFilterPanel filterPanel_;
    juce::Rectangle<int> chartArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqPaneView)
};

}  // namespace rta::view
