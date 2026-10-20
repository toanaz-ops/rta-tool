// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view.
//
// ALIGN-UI lane, task T5 (docs/plans/2026-10-20-align-ui-lane-plan.md,
// decisions 1-5, 7, 11): the sixth pane. The operator answers the wizard's
// four questions and the two output channels, presses MEASURE, and reads the
// verdict beside the crossover surface (the G18 phase and summation plots);
// SUM captures both outputs together and draws that measurement against the
// predicted sum.
//
// The pane owns WIDGETS ONLY. Answers and every run live in the AlignRunner
// MainComponentAlign owns, so switching to TRANSFER and back (which rebuilds
// the pane) loses nothing. Every question starts "not asked": nothing is
// pre-selected on the operator's behalf except the two output channels
// (decision 4: routing facts, not designer intent), and MEASURE stays
// disabled, with the reason beside it, until all four are answered. "NO SEED"
// is an explicit answer, not an absence.
//
// Topology and inversion come from the SAME lists the XOVER pane offers
// (TopologyChoices.h): the topology is asked in two places, from one table.
#pragma once

#include "measure/AlignRunner.h"
#include "view/AlignIrPanel.h"
#include "view/CrossoverSurface.h"
#include "view/PaneRegistry.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <cstdint>
#include <string>

namespace rta::view {

/// The pane's whole wiring, handed to `makePaneFactory`. `runner` is borrowed
/// from MainComponentAlign and must outlive every pane built from this.
struct AlignPaneBinding {
    rta::measure::AlignRunner* runner = nullptr;
    /// Wave B: SWEEP MAIN / SWEEP SUB and the IR pickers (AlignIrPanel.h). All
    /// optional: a pane built without them simply has dead sweep buttons.
    AlignIrHooks ir;
};

class AlignPaneView final : public juce::Component, public LibraryConsumer, private juce::Timer {
public:
    explicit AlignPaneView(AlignPaneBinding binding);
    ~AlignPaneView() override;

    /// Nothing here reads the library: the runner stores its captures itself.
    /// `override`: WorkspaceView forwards a library to every pane that
    /// accepts one, and the contract is that a pane with no use for it is
    /// not an error.
    void setLibrary(const rta::trace::TraceLibrary* library) override;
    void paint(juce::Graphics&) override;
    void resized() override;

    // --- test hooks: every name ends in ForTest and is referenced whole-word
    // from app/tests_juce/test_main_component_panes_align.cpp and
    // test_main_component_align.cpp (orphan_check's TEST HOOK category).
    [[nodiscard]] juce::ComboBox& mainOutputComboForTest() noexcept { return mainCombo_; }
    [[nodiscard]] juce::ComboBox& subOutputComboForTest() noexcept { return subCombo_; }
    [[nodiscard]] juce::ComboBox& highPassComboForTest() noexcept { return highPassCombo_; }
    [[nodiscard]] juce::ComboBox& topologyComboForTest() noexcept { return topologyCombo_; }
    [[nodiscard]] juce::ComboBox& inversionComboForTest() noexcept { return inversionCombo_; }
    [[nodiscard]] juce::ComboBox& seedComboForTest() noexcept { return seedCombo_; }
    [[nodiscard]] juce::Button& measureButtonForTest() noexcept { return measureButton_; }
    [[nodiscard]] juce::Button& sumButtonForTest() noexcept { return sumButton_; }
    [[nodiscard]] juce::Button& offsetButtonForTest() noexcept { return offsetButton_; }
    [[nodiscard]] juce::TextEditor& offsetEditorForTest() noexcept { return offsetEditor_; }
    [[nodiscard]] juce::String statusForTest() const { return statusLabel_.getText(); }
    [[nodiscard]] juce::String blockerTextForTest() const { return blockerLabel_.getText(); }
    [[nodiscard]] juce::String readoutForTest() const { return readoutLabel_.getText(); }
    [[nodiscard]] AlignIrPanel& irPanelForTest() noexcept { return irPanel_; }
    [[nodiscard]] juce::String irInfoForTest() const { return irInfoLabel_.getText(); }
    [[nodiscard]] const CrossoverSurface& surfaceForTest() const noexcept { return surface_; }
    [[nodiscard]] bool lastPaintDrewChartForTest() const noexcept { return lastPaintDrewChart_; }
    /// One turn of the pane's own 10 Hz timer (a test harness pumps no message loop).
    void tickForTest() { timerCallback(); }

private:
    void timerCallback() override;
    /// Everything that follows the runner: enable states, labels, the chart
    /// cache. One function, so the widgets can never disagree with the runner.
    void refresh();
    void rebuildOutputCombos();
    void rebuildSurface();
    void answersChanged();
    /// A pane rebuilt by a selector click starts from the runner's answers,
    /// not from "not asked": the answers outlive the pane.
    void restoreAnswers();

    AlignPaneBinding binding_;
    std::uint64_t lastRevision_ = ~std::uint64_t{ 0 };
    int lastOutputCount_ = -1;
    juce::String lastBlockerText_;
    bool lastQuiescent_ = true;  ///< the engine state SUM's enabled flag was last computed from

    CrossoverSurface surface_;
    bool ready_ = false;
    bool lastPaintDrewChart_ = false;

    juce::ComboBox mainCombo_, subCombo_, highPassCombo_, topologyCombo_, inversionCombo_, seedCombo_;
    juce::TextButton measureButton_{ "MEASURE" }, sumButton_{ "SUM" }, offsetButton_{ "SET OFFSET" };
    juce::TextEditor offsetEditor_;
    juce::Label statusLabel_, blockerLabel_, readoutLabel_, irInfoLabel_;
    AlignIrPanel irPanel_;

    juce::Rectangle<int> chartArea_, phaseArea_, summationArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlignPaneView)
};

}  // namespace rta::view
