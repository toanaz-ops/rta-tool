// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. MainComponent.cpp's own 400-line-cap split
// (owner decision 2026-09-26, "the gap"): before this task, MainComponent
// built exactly one hardcoded `rta` workspace pane and nothing in the
// running app could ever reach the `spl` or `transfer` panes, even though
// both are built and tested (docs/HUMAN-QA-QUEUE.md "Muc moi mo khi dong
// lane L6a"; docs/reports/009-spl-pro.md). This file is the selector's UI
// half: the RTA/TRANSFER/SPL radio group, its layout, and the rebuild that
// puts the chosen pane on screen. Member-function definitions, declared in
// MainComponent.h, no different in kind from anything else in that class.
//
// `selectPaneView` never touches `analysisThread_`'s SPL logging -- it only
// swaps which VIEW reads from the same, already-running analysis chain, the
// same way switching between two already-built RtaView instances would.
// SPL logging is `pollSplLogging()`'s decision alone (MainComponentSpl.cpp),
// driven off the capture bus, never off which pane happens to be visible.
#include "MainComponent.h"

void MainComponent::selectPaneView(rta::view::PaneSelectorButton button) {
    const auto resolution = rta::view::decidePaneSelection(button);
    if (resolution.view == currentPaneView_) {
        return;  // idempotent: selecting the pane already showing does nothing
    }

    // WorkspaceView's factory is consumed once, synchronously, in its own
    // constructor (that class's header comment) -- there is no in-place
    // "reconfigure" call, so a pane switch means building a whole new one.
    // Reassigning workspace_ destroys the old WorkspaceView first (plain
    // unique_ptr assignment order), and juce::Component's own destructor
    // detaches a child from its parent automatically -- no explicit
    // removeChildComponent needed before this line.
    std::vector<rta::trace::PaneSpec> panes{rta::trace::PaneSpec{resolution.requested}};
    workspace_ = std::make_unique<rta::view::WorkspaceView>(std::move(panes), makePaneFactory(analysisThread_, eq_.binding(), align_.binding(), rt60_.binding()));
    workspace_->setLibrary(&library_);
    addAndMakeVisible(*workspace_);
    currentPaneView_ = resolution.view;

    paneSelector_.syncTo(currentPaneView_);
    // station-3 STORE task T6: enabled only on RTA/TRANSFER -- SPL has no
    // per-bin curve and XOVER/EQ are consumers, not producers, of stored
    // traces (research C2).
    storeButton_.setEnabled(currentPaneView_ == rta::view::PaneView::Rta ||
                            currentPaneView_ == rta::view::PaneView::Transfer);

    // The new pane has no bounds yet (WorkspaceView's constructor lays out
    // nothing -- resized() does): position it now rather than waiting for
    // the next resize event, the same reason setSyntheticMode() above
    // calls repaint() rather than leaving the next timer tick to catch up.
    resized();
}

// MainComponentPanes.cpp: session Open's own rebuild (multi-pane aware).
// Returns the first pane resolution that fell back, so Open's readout
// can report it (PR #43 verifier MEDIUM F2).
rta::view::PaneResolution MainComponent::restoreWorkspaceFromSession(std::vector<rta::trace::PaneSpec> panes) {
    // Session Open's own rebuild: same machinery as selectPaneView above,
    // but for a FULL (already-normalised, 1..3 pane) session-saved layout
    // rather than always exactly one. The selector radio group has no way
    // to express "more than one pane is showing" -- it is synced to the
    // FIRST pane's resolved view, so it agrees with SOMETHING on screen
    // rather than being left stale.
    //
    // Fix round (PR #43 verifier MEDIUM F2): WorkspaceView's own constructor
    // resolves each pane's view string but deliberately never reports a
    // fallback (WorkspaceView.cpp's own comment: "the seam that owes this a
    // report is whatever future code path decodes a session file's [pane]
    // sections" -- this is that seam). Resolve every pane's view HERE first,
    // so an unrecognised name can be reported, before handing the same
    // strings to WorkspaceView to build from.
    rta::view::PaneResolution report;  // default: fellBack == false
    for (const auto& spec : panes) {
        const auto resolution = rta::view::resolvePaneView(spec.view);
        if (resolution.fellBack && !report.fellBack) report = resolution;
    }
    const auto firstView =
        panes.empty() ? rta::view::PaneView::Rta : rta::view::resolvePaneView(panes.front().view).view;

    workspace_ = std::make_unique<rta::view::WorkspaceView>(std::move(panes), makePaneFactory(analysisThread_, eq_.binding(), align_.binding(), rt60_.binding()));
    workspace_->setLibrary(&library_);
    addAndMakeVisible(*workspace_);
    currentPaneView_ = firstView;

    paneSelector_.syncTo(currentPaneView_);
    // T6 acceptance: this path (session Open) needs the same sync
    // selectPaneView() above already gets, not only the button-click half --
    // see that function's own comment on the identical omission this exact
    // shape already bit once (MainComponentPanes.cpp:133-137's XOVER toggle).
    storeButton_.setEnabled(currentPaneView_ == rta::view::PaneView::Rta ||
                            currentPaneView_ == rta::view::PaneView::Transfer);

    resized();
    return report;
}
