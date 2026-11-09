// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. MainComponent.cpp's own 400-line-cap split
// (owner decision 2026-09-26, "the gap"): before this task, MainComponent
// built exactly one hardcoded `rta` workspace pane and nothing in the
// running app could ever reach the `spl` or `transfer` panes, even though
// both are built and tested (docs/HUMAN-QA-QUEUE.md "Muc moi mo khi dong
// lane L6a"; docs/reports/009-spl-pro.md). This file is the selector's UI
// half: the selector buttons, their layout, and the rebuild that
// puts the chosen pane on screen. HW-176 (owner card, 2026-11-09) adds `+ PANE`
// (arm: the next view press APPENDS) and `- PANE` (drop the last pane); the
// rules are pure and live in view/PaneSelectorDecision.h. Member-function definitions, declared in
// MainComponent.h, no different in kind from anything else in that class.
//
// `selectPaneView` never touches `analysisThread_`'s SPL logging -- it only
// swaps which VIEW reads from the same, already-running analysis chain, the
// same way switching between two already-built RtaView instances would.
// SPL logging is `pollSplLogging()`'s decision alone (MainComponentSpl.cpp),
// driven off the capture bus, never off which pane happens to be visible.
#include "MainComponent.h"

#include <cctype>
#include <string>

namespace {

/// Every view the layout shows, for readouts: "RTA + TRANSFER".
std::string layoutSummary(const std::vector<rta::trace::PaneSpec>& panes) {
    std::string out;
    for (const auto& pane : panes) {
        if (!out.empty()) out += " + ";
        auto name = rta::view::paneViewName(rta::view::resolvePaneView(pane.view).view);
        for (auto& ch : name) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        out += name;
    }
    return out;
}

}  // namespace

// The one path that REPLACES the workspace (HW-176 T1): a plain view press and
// session Open. They used to carry two copies of build/setLibrary/syncTo/
// storeButton_; a fix to one (the XOVER-unlit bug of PR #45, the STORE-enable
// omission of T6) kept missing the other. It rebuilds every pane, so per-pane
// state (a TRANSFER source toggle, XOVER picks) resets -- as it always did.
// `+ PANE` / `- PANE` do NOT come through here: they change one pane through
// WorkspaceView::appendPane / removeLastPane and leave the rest alone. The
// constructor's initial workspace is built in MainComponent's init list.
//
// A replace is a rebuild from the specs (appendPane/removeLastPane above are
// the only in-place edits). Reassigning workspace_
// destroys the old WorkspaceView first (plain unique_ptr assignment order),
// and juce::Component's own destructor detaches a child from its parent
// automatically -- no explicit removeChildComponent needed.
void MainComponent::rebuildWorkspace(std::vector<rta::trace::PaneSpec> panes) {
    const auto firstView =
        panes.empty() ? rta::view::PaneView::Rta : rta::view::resolvePaneView(panes.front().view).view;

    workspace_ = std::make_unique<rta::view::WorkspaceView>(
        std::move(panes), makePaneFactory(analysisThread_, eq_.binding(), align_.binding(), rt60_.binding(), ir_.binding()));
    workspace_->setLibrary(&library_);
    addAndMakeVisible(*workspace_);
    // currentPaneView_ is still the FIRST pane's view: STORE and a dozen tests
    // read it as "the primary pane". Which panes are showing is
    // currentPaneViews(), read off the live workspace.
    currentPaneView_ = firstView;
    paneAddArmed_ = false;  // the arm is one-shot: any rebuild consumes it

    refreshPaneControls();

    // The new workspace has no bounds yet (WorkspaceView's constructor lays
    // out nothing -- resized() does): position it now rather than waiting for
    // the next resize event.
    resized();
}

// What follows an append or a remove: the first pane (and so currentPaneView_)
// is unchanged, the arm is spent, and the controls re-derive from the layout.
void MainComponent::finishPaneEdit() {
    paneAddArmed_ = false;
    refreshPaneControls();
    resized();
}

std::vector<rta::view::PaneView> MainComponent::currentPaneViews() const {
    return rta::view::viewsOf(workspace_->paneSpecs());
}

// Everything that depends on WHICH panes are showing, in one place so no
// build path can forget one of them (see rebuildWorkspace above).
void MainComponent::refreshPaneControls() {
    const auto views = currentPaneViews();
    paneSelector_.syncTo(views);
    paneSelector_.syncEdit(paneAddArmed_, rta::view::canAddPane(views.size()),
                           rta::view::canRemovePane(views.size()));
    // STORE (station-3 T6, HW-176 T4): enabled when ANY pane is RTA or
    // TRANSFER -- an [xover, transfer] layout used to leave it disabled
    // because only the first pane was consulted. SPL has no per-bin curve and
    // XOVER/EQ/ALIGN/RT60 consume stored traces rather than produce them.
    storeButton_.setEnabled(!rta::view::storableKinds(views).empty());
    paneReadout_.setText(paneAddArmed_ ? "panes: press a view to ADD it beside the others"
                                       : "panes: " + layoutSummary(workspace_->paneSpecs()),
                         juce::dontSendNotification);
}

void MainComponent::selectPaneView(rta::view::PaneSelectorButton button) {
    const auto edit = rta::view::decideViewPress(workspace_->paneSpecs(), button, paneAddArmed_);
    if (!edit.refusal.empty()) {
        paneAddArmed_ = false;
        refreshPaneControls();
        paneReadout_.setText(edit.refusal, juce::dontSendNotification);
        return;
    }
    if (!edit.changed) {
        // Idempotent: the one pane already showing this view. Pressing a view
        // is still the end of an armed add, so the arm clears either way.
        paneAddArmed_ = false;
        refreshPaneControls();
        return;
    }
    if (paneAddArmed_) {
        // Append ONE pane; the existing components keep their state. The
        // decision helper already checked the cap, appendPane re-checks it.
        workspace_->appendPane(edit.panes.back());
        finishPaneEdit();
        return;
    }
    rebuildWorkspace(edit.panes);
}

void MainComponent::toggleAddPaneArm() {
    if (!rta::view::canAddPane(workspace_->paneSpecs().size())) {
        paneAddArmed_ = false;
        refreshPaneControls();
        paneReadout_.setText("panes: at most " + juce::String(rta::trace::kMaxPanes) +
                                 " panes -- remove one first",
                             juce::dontSendNotification);
        return;
    }
    paneAddArmed_ = !paneAddArmed_;
    refreshPaneControls();
}

void MainComponent::removeLastPane() {
    const auto edit = rta::view::decideRemoveLast(workspace_->paneSpecs());
    if (!edit.refusal.empty()) {
        refreshPaneControls();
        paneReadout_.setText(edit.refusal, juce::dontSendNotification);
        return;
    }
    workspace_->removeLastPane();
    finishPaneEdit();
}

// MainComponentPanes.cpp: session Open's own rebuild (multi-pane aware).
// Returns the first pane resolution that fell back, so Open's readout
// can report it (PR #43 verifier MEDIUM F2).
rta::view::PaneResolution MainComponent::restoreWorkspaceFromSession(std::vector<rta::trace::PaneSpec> panes) {
    // PR #43 verifier MEDIUM F2: WorkspaceView's own constructor resolves each
    // pane's view string but deliberately never reports a fallback. Resolve
    // every pane's view HERE first, so an unrecognised name can be reported.
    rta::view::PaneResolution report;  // default: fellBack == false
    for (const auto& spec : panes) {
        const auto resolution = rta::view::resolvePaneView(spec.view);
        if (resolution.fellBack && !report.fellBack) report = resolution;
    }
    rebuildWorkspace(std::move(panes));
    return report;
}
