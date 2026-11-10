// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. H11 W3 (docs/dsp/2026-11-10-g22-offline-wav-transfer.md D7-D9):
// the operator's path to an offline transfer function -- LOAD WAV beside STORE, then a row to
// pick the reference and measurement channel and a range, ANALYSE, and a readout. In the shape
// MainComponentSweep gives its half: a small class MainComponent owns, so the composition root
// gains a member, a layout call and a poll, not a state machine.
//
// LOAD opens a short-lived reader on THIS thread only to learn the channel count, rate and
// length for the controls. ANALYSE hands the file and the request to `WavTransferJob`, whose
// worker opens its own reader (WavFileSource is single-thread). The finished outcome arrives
// through `poll()` (MainComponent's 2 Hz timer) and a success becomes one trace in library
// group `WAV`. Nothing here runs on the audio thread.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <az_ui/az_ui.h>

#include "measure/WavTransferJob.h"
#include "trace/TraceLibrary.h"

#include <memory>
#include <string>

class MainComponentWav final {
public:
    /// Height of everything `layout` places: four control rows and a two-row readout, with a
    /// gap between each. MainComponentLayout.cpp adds it to the rail's scrolling rows.
    static constexpr int kRowsHeight = 6 * az::ui::touchTarget + 4 * az::ui::gap;

    explicit MainComponentWav(rta::trace::TraceLibrary& library);

    void attachTo(juce::Component& parent);
    void layout(juce::Rectangle<int> area);

    /// The LOAD WAV button: an asynchronous chooser whose result goes to `performLoad`.
    void loadClicked();
    /// What the chooser's callback does. False (and a readout saying why) for a file that is not
    /// a readable WAV/AIFF; a mono file loads but refuses with the record's sentence.
    bool performLoad(const juce::File& file);
    /// ANALYSE: starts the worker on the current file and controls; a press while one runs
    /// replaces it (newest wins).
    void analyseClicked();
    /// From MainComponent's timer: takes a finished outcome and stores its trace.
    void poll();

    // --- test hooks, each referenced by spelling from app/tests_juce ---------
    void setControlsForTest(int refChannel1Based, int measChannel1Based, double startSeconds,
                            double lengthSeconds);
    /// Pumps `poll()` until the worker is idle (bounded); false on timeout.
    [[nodiscard]] bool waitForJobForTest(int timeoutMs);
    [[nodiscard]] juce::String readoutForTest() const { return readout_.getText(); }
    [[nodiscard]] juce::String fileTextForTest() const { return fileLabel_.getText(); }
    [[nodiscard]] bool analyseEnabledForTest() const { return analyseButton_.isEnabled(); }
    [[nodiscard]] const juce::Component& loadButtonForTest() const noexcept { return loadButton_; }
    [[nodiscard]] const juce::Component& analyseButtonForTest() const noexcept { return analyseButton_; }
    [[nodiscard]] const juce::ComboBox& refComboForTest() const noexcept { return refCombo_; }
    [[nodiscard]] const juce::ComboBox& measComboForTest() const noexcept { return measCombo_; }

private:
    void say(const juce::String& text) { readout_.setText(text, juce::dontSendNotification); }
    void setControlsEnabled(bool enabled);
    void finish(rta::measure::WavTransferOutcome& outcome);

    rta::trace::TraceLibrary& library_;
    rta::measure::WavTransferJob job_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::File file_;

    juce::TextButton loadButton_{"LOAD WAV"};
    juce::Label fileLabel_;
    juce::Label refLegend_{{}, "REF"};
    juce::ComboBox refCombo_;
    juce::Label measLegend_{{}, "MEAS"};
    juce::ComboBox measCombo_;
    juce::Label startLegend_{{}, "START s"};
    juce::TextEditor startEditor_;
    juce::Label lengthLegend_{{}, "LENGTH s"};
    juce::TextEditor lengthEditor_;
    juce::TextButton analyseButton_{"ANALYSE"};
    juce::Label readout_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponentWav)
};
