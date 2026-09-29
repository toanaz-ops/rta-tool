// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src.
//
// L7-EQ UI task T6 (docs/plans/2026-09-29-eq-ui-lane-plan.md, decisions D8-D13).
// MainComponent's EQ half, in the shape MainComponentSession already gives
// SAVE / OPEN: a small class MainComponent owns as a member, so MainComponent.h
// gains one line, not a dozen. It owns the EqPaneModel -- NOT the pane, which
// MainComponentPanes.cpp rebuilds on every selector click -- and the export
// dialogs and writes.
//
// Every export is an async FileChooser in save mode with the overwrite
// warning, defaulting to Documents/RTA Tool/fir/<stem>.<ext> where the stem is
// firFilenameStem (D13). Cancelling does nothing. The write happens on the
// message thread, as UTF-8 with no BOM (EQ record Sec.7 amendment 3: a BOM is
// what made a lowshelf import as a peaking filter). The perform* halves take a
// File and never open a dialog, so a test can prove the bytes without one.
#pragma once

#include "measure/EqPaneModel.h"
#include "view/EqPaneView.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>

class MainComponentEq {
public:
    MainComponentEq();

    /// The wiring `makePaneFactory` gives every EqPaneView it builds.
    [[nodiscard]] rta::view::EqPaneBinding binding();

    /// The three EXPORT buttons' handlers: open the save dialog, then perform.
    void exportFirTextClicked();
    void exportFirWavClicked();
    void exportListClicked();

    /// The write halves. Each returns true on success and otherwise leaves a
    /// named reason in the model's status (the pane's readout). Refuses --
    /// never guesses -- when the FIR phase or length has not been answered.
    bool performExportFirText(const juce::File& file);
    bool performExportFirWav(const juce::File& file);
    bool performExportList(const juce::File& file);

    /// Documents/RTA Tool/fir/eq_<fs>Hz_<N>taps_<lin|min>.<ext>; empty File
    /// until both FIR questions are answered.
    [[nodiscard]] juce::File defaultFirFile(const char* extension) const;

    [[nodiscard]] rta::measure::EqPaneModel& modelForTest() noexcept { return model_; }

private:
    void launchSave(const juce::String& title, const juce::File& defaultFile, const char* extension,
                    std::function<bool(const juce::File&)> perform);

    rta::measure::EqPaneModel model_;
    std::unique_ptr<juce::FileChooser> chooser_;
};
