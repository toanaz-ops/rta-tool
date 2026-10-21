// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See MainComponentPaneSelector.h.
#include "MainComponentPaneSelector.h"

#include <az_ui/az_ui.h>

void MainComponentPaneSelector::attach(juce::Component& parent, SelectFn onSelect) {
    using rta::view::PaneSelectorButton;
    // One radio group: JUCE mutually excludes toggle state within a group
    // sharing both a group id and a parent, the same "clicking one turns the
    // others off" contract modeSwitch_ gives itself alone with
    // setClickingTogglesState(true).
    constexpr int kPaneSelectorRadioGroupId = 1;
    for (auto* button : {&rtaButton_, &transferButton_, &splButton_, &xoverButton_, &eqButton_,
                         &alignButton_, &rt60Button_}) {
        button->setRadioGroupId(kPaneSelectorRadioGroupId, juce::dontSendNotification);
        button->setClickingTogglesState(true);
    }

    // Matches currentPaneView_'s own default (Rta) -- the button that reads
    // "selected" on first paint must agree with the pane workspace_ actually
    // shows, or a user's very first glance at the selector would be lying.
    rtaButton_.setToggleState(true, juce::dontSendNotification);

    rtaButton_.getProperties().set(az::ui::hintProperty, "spectrum analyser");
    transferButton_.getProperties().set(az::ui::hintProperty, "dual-FFT transfer function");
    splButton_.getProperties().set(az::ui::hintProperty, "SPL meter -- Leq, dose, Ln");
    xoverButton_.getProperties().set(az::ui::hintProperty,
                                     "crossover alignment -- G18, two stored traces");
    eqButton_.getProperties().set(az::ui::hintProperty, "auto EQ -- one stored TRANSFER trace");
    alignButton_.getProperties().set(az::ui::hintProperty,
                                     "align a main/sub crossover -- measures each output alone");
    rt60Button_.getProperties().set(az::ui::hintProperty,
                                    "reverberation time per octave -- T30, T20, EDT from a log sweep");

    rtaButton_.onClick = [onSelect] { onSelect(PaneSelectorButton::Rta); };
    transferButton_.onClick = [onSelect] { onSelect(PaneSelectorButton::Transfer); };
    splButton_.onClick = [onSelect] { onSelect(PaneSelectorButton::Spl); };
    xoverButton_.onClick = [onSelect] { onSelect(PaneSelectorButton::Xover); };
    eqButton_.onClick = [onSelect] { onSelect(PaneSelectorButton::Eq); };
    alignButton_.onClick = [onSelect] { onSelect(PaneSelectorButton::Align); };
    rt60Button_.onClick = [onSelect] { onSelect(PaneSelectorButton::Rt60); };

    parent.addAndMakeVisible(rtaButton_);
    parent.addAndMakeVisible(transferButton_);
    parent.addAndMakeVisible(splButton_);
    parent.addAndMakeVisible(xoverButton_);
    parent.addAndMakeVisible(eqButton_);
    parent.addAndMakeVisible(alignButton_);
    parent.addAndMakeVisible(rt60Button_);
}

void MainComponentPaneSelector::layout(juce::Rectangle<int> row) {
    // Same "(width - N-1 gaps) / N" shape MainComponent::resized() already
    // uses for the LOCATE/APPLY and CAL START/CAL END rows -- seven equal
    // cells, six gaps between them (grown from six when RT60 was added).
    const int buttonWidth = (row.getWidth() - az::ui::gap * 6) / 7;
    rtaButton_.setBounds(row.removeFromLeft(buttonWidth));
    row.removeFromLeft(az::ui::gap);
    transferButton_.setBounds(row.removeFromLeft(buttonWidth));
    row.removeFromLeft(az::ui::gap);
    splButton_.setBounds(row.removeFromLeft(buttonWidth));
    row.removeFromLeft(az::ui::gap);
    xoverButton_.setBounds(row.removeFromLeft(buttonWidth));
    row.removeFromLeft(az::ui::gap);
    eqButton_.setBounds(row.removeFromLeft(buttonWidth));
    row.removeFromLeft(az::ui::gap);
    alignButton_.setBounds(row.removeFromLeft(buttonWidth));
    row.removeFromLeft(az::ui::gap);
    rt60Button_.setBounds(row);
}

void MainComponentPaneSelector::syncTo(rta::view::PaneView view) {
    using rta::view::PaneView;
    rtaButton_.setToggleState(view == PaneView::Rta, juce::dontSendNotification);
    transferButton_.setToggleState(view == PaneView::Transfer, juce::dontSendNotification);
    splButton_.setToggleState(view == PaneView::Spl, juce::dontSendNotification);
    // PR #45 fix round 3 (verifier, PR #43 reconciliation checklist item 3):
    // this line was missing here, unlike selectPaneView() above which
    // already syncs all four -- opening a saved XOVER session left every
    // selector button unlit even though the xover pane itself was showing.
    xoverButton_.setToggleState(view == PaneView::Xover, juce::dontSendNotification);
    eqButton_.setToggleState(view == PaneView::Eq, juce::dontSendNotification);
    alignButton_.setToggleState(view == PaneView::Align, juce::dontSendNotification);
    rt60Button_.setToggleState(view == PaneView::Rt60, juce::dontSendNotification);
}
