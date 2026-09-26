// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. Wires SessionStore/SessionCodec (built and
// unit-tested in lane L5a, extended by L6b/L6a) into the running app: before
// this, `rtatool.exe` could not save or load anything -- close the app and
// the stored traces are gone.
//
// Same shape MainComponentRail.h already gives (that file's own comment):
// MainComponent grants exactly one member the state it does not want to
// carry in its own 400-line-capped header. UNLIKE MainComponentRail, this
// class also carries the actual business logic (the SessionStore/SessionCodec
// calls) rather than only owning widgets, because that logic needs nothing
// from MainComponent except a trace library and a way to rebuild the
// workspace -- and the two callbacks below are how it reaches those without
// ever including AnalysisThread.h or WorkspaceView.h, the same
// factory-seam reasoning `view/PaneRegistry.h`'s `LibraryConsumer` and
// `WorkspaceView::PaneFactory` already use one layer down.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "trace/TraceLibrary.h"
#include "trace/Workspace.h"
#include "view/PaneRegistry.h"

#include <functional>
#include <memory>
#include <vector>

/// Owns the SAVE SESSION / OPEN SESSION buttons, their shared readout, and
/// the async juce::FileChooser both need. All session I/O runs synchronously
/// on the message thread, inside the FileChooser's own completion callback --
/// the same shape `MainComponent::exportReportClicked()` already uses for its
/// own one-off file write (MainComponentSpl.cpp), never the audio or analysis
/// thread (project CLAUDE.md "Real-time safety").
class MainComponentSession final {
public:
    /// Rebuilds the workspace from a loaded session's `[pane]` sections
    /// (already normalised via `rta::trace::normalisePanes` -- never empty).
    /// MainComponent supplies this because only it knows AnalysisThread and
    /// `makePaneFactory`; this class never includes either. Returns the
    /// resolution of the first pane that did NOT resolve (`fellBack==false`,
    /// default-constructed, if every pane resolved) -- `PaneRegistry.h`'s
    /// `PaneResolution` says the caller REPORTS a fallback; this is that
    /// report reaching all the way to the readout (fix round, PR #43
    /// verifier MEDIUM F2).
    using RestorePaneView = std::function<rta::view::PaneResolution(std::vector<rta::trace::PaneSpec>)>;
    /// What Save should write for the pane CURRENTLY showing. MainComponent
    /// supplies this because only it knows `currentPaneView_`.
    using CurrentPaneSpec = std::function<rta::trace::PaneSpec()>;

    MainComponentSession(rta::trace::TraceLibrary& library, CurrentPaneSpec currentPaneSpec,
                         RestorePaneView restorePaneView);

    /// Adds the two buttons and the readout directly into `parent` -- no
    /// intervening container, the same flat shape `exportReportButton_`/
    /// `exportReportReadout_` already use on MainComponent itself.
    void attachTo(juce::Component& parent);

    /// Lays out one button row (SAVE | OPEN) followed by the readout, within
    /// whatever `area` the caller has already reserved.
    void layout(juce::Rectangle<int> area);

private:
    friend struct MainComponentTestAccess;  // session save/open test seam
    void performSave(const juce::File& folder);
    void performOpen(const juce::File& folder);
    [[nodiscard]] juce::String readoutForTest() const { return readout_.getText(); }

    void saveClicked();
    void openClicked();

    rta::trace::TraceLibrary& library_;
    CurrentPaneSpec currentPaneSpec_;
    RestorePaneView restorePaneView_;

    juce::TextButton saveButton_{"SAVE SESSION"};
    juce::TextButton openButton_{"OPEN SESSION"};
    juce::Label readout_;
    // Kept alive between launchAsync() and its callback -- a local variable
    // would be destroyed the instant saveClicked()/openClicked() returns,
    // before the (asynchronous, possibly OS-native) dialog ever resolves.
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponentSession)
};
