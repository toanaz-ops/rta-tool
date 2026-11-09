// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view.
//
// H9 / L4c task A3 (docs/plans/2026-11-09-h9-l4c-ir-gate-plan.md, record
// docs/dsp/2026-11-09-l4c-ir-gate.md sec. 5, decision 5): the eighth pane. The
// operator picks an IR (the SWEEP traces whose IR is still in memory), drags the
// two gate markers over its energy-time curve, reads the gated magnitude and the
// lowest frequency the gate can speak for, and presses STORE GATED to put the
// result in the trace library, where TRANSFER draws it like any stored trace.
//
// The pane owns widgets only. The selection, the markers and the cached analysis
// live in MainComponentIr, which outlives every pane built here, so a selector
// click (which rebuilds the pane) loses nothing. Nothing here includes `rta/ir/*`:
// the gate arrives as `IrGateAnalysis`.
#pragma once

#include "measure/IrGateModel.h"
#include "view/IrPlots.h"
#include "view/PaneRegistry.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rta::view {

/// One library SWEEP trace whose IR is in memory.
struct IrChoice {
    std::string id;    ///< the library trace id (the IrStore key)
    std::string name;  ///< as the library shows it
};

/// The pane's whole wiring, handed to `makePaneFactory`. Every hook is optional
/// (a factory test builds a pane from an empty binding); MainComponentIr sets
/// them all.
struct IrPaneBinding {
    std::function<std::vector<IrChoice>()> choices;
    std::function<std::string()> selectedId;
    std::function<void(const std::string& traceId)> select;
    /// The ETC of the selection; null before any pick.
    std::function<std::shared_ptr<const rta::measure::IrEtc>()> etc;
    /// The analysis of the CURRENT markers; null before any pick.
    std::function<std::shared_ptr<const rta::measure::IrGateAnalysis>()> analysis;
    std::function<rta::measure::IrGateMarkers()> markers;
    std::function<void(const rta::measure::IrGateMarkers&)> setMarkers;
    /// STORE GATED: adds the gated trace to the library and returns the sentence
    /// to show (what was stored, or why nothing was).
    std::function<std::string()> storeGated;
    /// H10: which picture the lower plot shows, and the G24 decomposition of the
    /// gated response behind PHASE and EXCESS. `excess` is null until a settled gate
    /// has been decomposed; `excessPending` says one is running.
    std::function<rta::measure::IrLowerPlot()> lowerPlot;
    std::function<void(rta::measure::IrLowerPlot)> setLowerPlot;
    std::function<std::shared_ptr<const rta::measure::IrExcessPhase>()> excess;
    std::function<bool()> excessPending;
    /// The hand let go of a marker: the moment the decomposition may start.
    std::function<void()> settle;
    /// Bumps whenever the selection, the markers or the analysis changed.
    std::function<std::uint64_t()> revision;
};

class IrPaneView final : public juce::Component, public LibraryConsumer, private juce::Timer {
public:
    explicit IrPaneView(IrPaneBinding binding);
    ~IrPaneView() override;

    /// Nothing here reads the library: the controller lists the traces itself.
    void setLibrary(const rta::trace::TraceLibrary* library) override;
    void paint(juce::Graphics&) override;
    void resized() override;

    // --- test hooks: every name ends in ForTest and is referenced whole-word from
    // app/tests_juce/test_ir_pane_view.cpp (orphan_check's TEST HOOK category).
    [[nodiscard]] juce::ComboBox& irComboForTest() noexcept { return irCombo_; }
    [[nodiscard]] juce::Button& storeButtonForTest() noexcept { return storeButton_; }
    [[nodiscard]] juce::String readoutForTest() const { return readoutLabel_.getText(); }
    [[nodiscard]] juce::String statusForTest() const { return statusLabel_.getText(); }
    [[nodiscard]] IrEtcPlot& etcPlotForTest() noexcept { return etcPlot_; }
    [[nodiscard]] const IrGatedPlot& gatedPlotForTest() const noexcept { return gatedPlot_; }
    [[nodiscard]] juce::Button& lowerPlotButtonForTest(rta::measure::IrLowerPlot mode) noexcept {
        return *modeButtons_[static_cast<std::size_t>(mode)];
    }
    [[nodiscard]] juce::StringArray pickerTextsForTest() const;
    /// One turn of the pane's own 10 Hz timer (a test harness pumps no message loop).
    void tickForTest() { timerCallback(); }

private:
    void timerCallback() override;
    void refresh();
    void refreshPicker();
    void picked();
    void markersMoved(const rta::measure::IrGateMarkers& markers);
    void storeClicked();
    void lowerPlotPicked(rta::measure::IrLowerPlot mode);

    IrPaneBinding binding_;
    std::uint64_t lastRevision_ = ~std::uint64_t{0};
    juce::String lastPickerKey_;
    std::vector<IrChoice> choices_;

    juce::ComboBox irCombo_;
    juce::TextButton storeButton_{"STORE GATED"};
    juce::Label readoutLabel_, statusLabel_;
    std::array<std::unique_ptr<juce::TextButton>, 3> modeButtons_;  ///< MAG, PHASE, EXCESS
    IrEtcPlot etcPlot_;
    IrGatedPlot gatedPlot_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IrPaneView)
};

}  // namespace rta::view
