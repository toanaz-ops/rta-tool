// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view.
//
// RT60-UI lane, task T4 (docs/plans/2026-10-20-rt60-ui-lane-plan.md, decisions
// 1, 2, 5, 7, 8, 9): the seventh pane. The operator picks a SWEEP trace (or
// presses SWEEP on a chosen output with a chosen TAIL, which stores one and
// picks it), and reads a table: one row per octave band, 63 Hz to 8 kHz, with
// T30, T20, EDT*, the decay range reached, and -- where a figure was withheld --
// the reason, with its numbers.
//
// The pane owns WIDGETS ONLY. The cached analysis, the selection and the two
// settings live in MainComponentRt60, which outlives every pane built here, so a
// selector click (which rebuilds the pane) loses nothing. The words and numbers
// come from measure/Rt60Readout.h, which a test can read with no window.
//
// Nothing here includes `rta/ir/*`: the analysis arrives as `Rt60Result`.
#pragma once

#include "measure/Rt60Analysis.h"
#include "measure/Rt60EnsembleReadout.h"
#include "measure/Rt60Readout.h"
#include "view/Rt60Chart.h"
#include "view/PaneRegistry.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rta::view {

/// One library SWEEP trace the picker lists. `hasIr` false means the IR is not in
/// memory (the trace was opened from a file, or the store dropped it): it is
/// listed WITH that reason, never hidden (plan decision 9).
struct Rt60Choice {
    std::string id;    ///< the library trace id (the IrStore key)
    std::string name;  ///< the trace's name, as the library shows it
    bool hasIr = false;
};

/// What the pane's two combos set, owned by MainComponentRt60 so a rebuilt pane
/// starts where the operator left them. Output is 0-based; the default is out 1
/// and 3 s, which is also what the rail's SWEEP button does.
struct Rt60Settings {
    int outputChannel = 0;
    double tailSeconds = 3.0;
    /// Octave or third-octave rows (wave B, T11). The controller re-analyses on a change.
    rta::measure::Rt60BandSet bandSet = rta::measure::Rt60BandSet::Octave;
    /// The row whose decay curve the chart shows (T10); -1 is "pick one for me".
    /// An index into the CURRENT band set, reset whenever the set changes.
    int selectedBand = -1;
};

/// The pane's whole wiring, handed to `makePaneFactory`. Every hook is optional
/// (a factory test builds a pane from an empty binding); MainComponentRt60 sets
/// them all.
struct Rt60PaneBinding {
    /// Library SWEEP traces, newest first.
    std::function<std::vector<Rt60Choice>()> choices;
    std::function<std::string()> selectedId;
    /// Picking a trace analyses it (or records that it has no IR).
    std::function<void(const std::string& traceId)> select;
    /// The cached analysis of the selected trace; null before any pick.
    std::function<std::shared_ptr<const rta::measure::Rt60Result>()> result;
    std::function<std::string()> selectedName;
    /// False once the selected trace's IR has left the store (the result stays shown).
    std::function<bool()> selectedIrInMemory;
    /// Bumps whenever the selection or the result changed.
    std::function<std::uint64_t()> revision;
    /// SWEEP: on this 0-based output with this tail. False when refused.
    std::function<bool(int outputChannel, double tailSeconds)> sweep;
    std::function<bool()> sweepBusy;
    std::function<std::string()> sweepReadout;
    std::function<int()> outputChannels;
    Rt60Settings* settings = nullptr;

    // --- wave B -------------------------------------------------------------
    /// OCTAVE / THIRD: re-analyses the selected trace (and the ensemble) in the new set.
    std::function<void(rta::measure::Rt60BandSet)> setBandSet;
    /// The ticked ensemble members' trace ids, in the order they were ticked.
    std::function<std::vector<std::string>()> ensembleIds;
    /// Ticks or unticks a trace; false when the ensemble is full (eight) or the trace has no IR.
    std::function<bool(const std::string& traceId)> toggleEnsemble;
    std::function<void()> clearEnsemble;
    /// The ensemble's analysis; null below two members or while it runs.
    std::function<std::shared_ptr<const rta::measure::Rt60EnsembleResult>()> ensembleResult;
    std::function<std::vector<std::string>()> ensembleNames;
    /// True while the worker thread holds a job: the table is empty by design.
    std::function<bool()> analysing;
};

class Rt60PaneView final : public juce::Component, public LibraryConsumer, private juce::Timer {
public:
    explicit Rt60PaneView(Rt60PaneBinding binding);
    ~Rt60PaneView() override;

    /// Nothing here reads the library: the controller lists the traces itself.
    void setLibrary(const rta::trace::TraceLibrary* library) override;
    void paint(juce::Graphics&) override;
    void resized() override;

    // --- test hooks: every name ends in ForTest and is referenced whole-word
    // from app/tests_juce/test_main_component_panes_rt60.cpp and
    // test_main_component_rt60.cpp (orphan_check's TEST HOOK category).
    [[nodiscard]] juce::ComboBox& irComboForTest() noexcept { return irCombo_; }
    [[nodiscard]] juce::ComboBox& outputComboForTest() noexcept { return outputCombo_; }
    [[nodiscard]] juce::ComboBox& tailComboForTest() noexcept { return tailCombo_; }
    [[nodiscard]] juce::Button& sweepButtonForTest() noexcept { return sweepButton_; }
    [[nodiscard]] juce::String headerForTest() const { return headerLabel_.getText(); }
    [[nodiscard]] juce::String statusForTest() const { return statusLabel_.getText(); }
    [[nodiscard]] juce::String adviceForTest() const { return adviceLabel_.getText(); }
    [[nodiscard]] juce::String footnoteForTest() const { return footnoteLabel_.getText(); }
    /// The table as text, one entry per line, exactly the strings the pane paints.
    [[nodiscard]] const std::vector<std::string>& tableLinesForTest() const noexcept { return tableLines_; }
    [[nodiscard]] const std::string& wholeRefusalForTest() const noexcept { return wholeRefusal_; }
    [[nodiscard]] bool lastPaintDrewTableForTest() const noexcept { return lastPaintDrewTable_; }
    /// The text of the picker's entries, in order (index 0 is the first trace).
    [[nodiscard]] juce::StringArray pickerTextsForTest() const;
    /// One turn of the pane's own 10 Hz timer (a test harness pumps no message loop).
    void tickForTest() { timerCallback(); }

    // Wave B hooks, referenced whole-word from the same two test files.
    [[nodiscard]] juce::Button& ensembleButtonForTest() noexcept { return ensembleButton_; }
    [[nodiscard]] juce::Button& bandSetButtonForTest() noexcept { return bandSetButton_; }
    [[nodiscard]] const Rt60Chart& chartForTest() const noexcept { return chart_; }
    /// The ensemble menu as the operator would see it, one entry per line, prefixed
    /// "[x] " when ticked and "[ ] " when not, and "(full)" suffixed when disabled.
    [[nodiscard]] juce::StringArray ensembleMenuForTest() const;
    /// Runs what the menu does for the entry at `index` of `ensembleMenuForTest`.
    void chooseEnsembleEntryForTest(int index) { chooseEnsembleEntry(index); }
    /// Clicks a table row, 0-based among the bands shown.
    void clickRowForTest(int row) { selectRow(row); }
    [[nodiscard]] bool showingEnsembleForTest() const noexcept { return ensembleShown_ != nullptr; }
    [[nodiscard]] int chartBandIndexForTest() const noexcept { return chartBand_; }

private:
    void timerCallback() override;
    void refresh();
    void refreshPicker();
    void refreshOutputCombo();
    void picked();
    void mouseDown(const juce::MouseEvent&) override;
    void layoutBody();
    void selectRow(int row);
    void refreshChart();
    void refreshButtons();
    [[nodiscard]] std::size_t bandCount() const;
    [[nodiscard]] juce::PopupMenu buildEnsembleMenu(std::vector<std::string>* idsByItem) const;
    void chooseEnsembleEntry(int index);

    Rt60PaneBinding binding_;
    std::uint64_t lastRevision_ = ~std::uint64_t{0};
    int lastOutputCount_ = -1;
    juce::String lastPickerKey_;
    std::vector<Rt60Choice> choices_;

    // What the table shows, rebuilt on a revision change.
    std::shared_ptr<const rta::measure::Rt60Result> shown_;
    std::vector<rta::measure::Rt60Row> rows_;
    std::vector<std::string> tableLines_;
    std::string wholeRefusal_;
    bool lastPaintDrewTable_ = false;

    // Wave B: the ensemble that replaces the single table when two or more
    // captures are ticked, the row the chart follows, and what the menu offers.
    std::shared_ptr<const rta::measure::Rt60EnsembleResult> ensembleShown_;
    int chartBand_ = -1;
    bool columnsAreEnsemble_ = false;
    bool analysingShown_ = false;
    int rowHeight_ = 26;
    juce::Rectangle<int> rowsArea_;

    juce::ComboBox irCombo_, outputCombo_, tailCombo_;
    juce::TextButton sweepButton_{"SWEEP"};
    juce::TextButton ensembleButton_{"ENSEMBLE"};
    juce::TextButton bandSetButton_{"OCTAVE"};
    Rt60Chart chart_;
    juce::Label headerLabel_, statusLabel_, adviceLabel_, footnoteLabel_;
    juce::Rectangle<int> tableArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Rt60PaneView)
};

}  // namespace rta::view
