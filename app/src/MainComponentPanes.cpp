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

void MainComponent::wirePaneSelectorButtons() {
    // One radio group: JUCE mutually excludes toggle state within a group
    // sharing both a group id and a parent, the same "clicking one turns the
    // others off" contract modeSwitch_ gives itself alone with
    // setClickingTogglesState(true).
    constexpr int kPaneSelectorRadioGroupId = 1;
    for (auto* button : {&paneRtaButton_, &paneTransferButton_, &paneSplButton_}) {
        button->setRadioGroupId(kPaneSelectorRadioGroupId, juce::dontSendNotification);
        button->setClickingTogglesState(true);
    }

    // Matches currentPaneView_'s own default (Rta) -- the button that reads
    // "selected" on first paint must agree with the pane workspace_ actually
    // shows, or a user's very first glance at the selector would be lying.
    paneRtaButton_.setToggleState(true, juce::dontSendNotification);

    paneRtaButton_.getProperties().set(az::ui::hintProperty, "spectrum analyser");
    paneTransferButton_.getProperties().set(az::ui::hintProperty, "dual-FFT transfer function");
    paneSplButton_.getProperties().set(az::ui::hintProperty, "SPL meter -- Leq, dose, Ln");

    paneRtaButton_.onClick = [this] { selectPaneView(rta::view::PaneSelectorButton::Rta); };
    paneTransferButton_.onClick = [this] { selectPaneView(rta::view::PaneSelectorButton::Transfer); };
    paneSplButton_.onClick = [this] { selectPaneView(rta::view::PaneSelectorButton::Spl); };

    addAndMakeVisible(paneRtaButton_);
    addAndMakeVisible(paneTransferButton_);
    addAndMakeVisible(paneSplButton_);
}

void MainComponent::layoutPaneSelectorRow(juce::Rectangle<int> row) {
    // Same "(width - 2 gaps) / 3" shape MainComponent::resized() already
    // uses for the LOCATE/APPLY and CAL START/CAL END rows -- three equal
    // cells, two gaps between them.
    const int buttonWidth = (row.getWidth() - az::ui::gap * 2) / 3;
    paneRtaButton_.setBounds(row.removeFromLeft(buttonWidth));
    row.removeFromLeft(az::ui::gap);
    paneTransferButton_.setBounds(row.removeFromLeft(buttonWidth));
    row.removeFromLeft(az::ui::gap);
    paneSplButton_.setBounds(row);
}

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
    workspace_ = std::make_unique<rta::view::WorkspaceView>(std::move(panes), makePaneFactory(analysisThread_));
    workspace_->setLibrary(&library_);
    addAndMakeVisible(*workspace_);
    currentPaneView_ = resolution.view;

    paneRtaButton_.setToggleState(currentPaneView_ == rta::view::PaneView::Rta, juce::dontSendNotification);
    paneTransferButton_.setToggleState(currentPaneView_ == rta::view::PaneView::Transfer,
                                       juce::dontSendNotification);
    paneSplButton_.setToggleState(currentPaneView_ == rta::view::PaneView::Spl, juce::dontSendNotification);

    // The new pane has no bounds yet (WorkspaceView's constructor lays out
    // nothing -- resized() does): position it now rather than waiting for
    // the next resize event, the same reason setSyntheticMode() above
    // calls repaint() rather than leaving the next timer tick to catch up.
    resized();
}
