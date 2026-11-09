// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See MainComponentPaneSelector.h.
#include "MainComponentPaneSelector.h"

#include <az_ui/az_ui.h>

#include <utility>

void MainComponentPaneSelector::attach(juce::Component& parent, SelectFn onSelect,
                                       std::function<void()> onToggleAdd,
                                       std::function<void()> onRemoveLast) {
    using rta::view::PaneSelectorButton;
    // No radio group (HW-176): a multi-pane workspace lights one button per
    // pane, and JUCE's radio group would force all but one off. Each button
    // is a plain button; syncTo() owns its toggle state, and a click never
    // flips it on its own (clickingTogglesState stays false).
    for (auto* button : {&rtaButton_, &transferButton_, &splButton_, &xoverButton_, &eqButton_,
                         &alignButton_, &rt60Button_, &addPaneButton_}) {
        button->setClickingTogglesState(false);
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
    addPaneButton_.getProperties().set(az::ui::hintProperty,
                                       "arm, then press a view: that view is ADDED beside the others");
    removePaneButton_.getProperties().set(az::ui::hintProperty, "remove the last pane");
    addPaneButton_.onClick = std::move(onToggleAdd);
    removePaneButton_.onClick = std::move(onRemoveLast);

    parent.addAndMakeVisible(rtaButton_);
    parent.addAndMakeVisible(transferButton_);
    parent.addAndMakeVisible(splButton_);
    parent.addAndMakeVisible(xoverButton_);
    parent.addAndMakeVisible(eqButton_);
    parent.addAndMakeVisible(alignButton_);
    parent.addAndMakeVisible(rt60Button_);
    parent.addAndMakeVisible(addPaneButton_);
    parent.addAndMakeVisible(removePaneButton_);
}

void MainComponentPaneSelector::layout(juce::Rectangle<int> row) {
    // Nine cells, eight gaps. The two edit cells are narrower than a view
    // cell ("+ PANE" is shorter than "TRANSFER"): a 7:2-cell split with the
    // edit cells at 2/3 of a view cell. At the 1100 px minimum window the row
    // is ~708 px.
    const int usable = row.getWidth() - az::ui::gap * 8;
    const int buttonWidth = usable * 3 / 25;           // 7 * 3 + 2 * 2 = 25 thirds
    const int editWidth = usable * 2 / 25;
    for (auto* button : {&rtaButton_, &transferButton_, &splButton_, &xoverButton_, &eqButton_,
                         &alignButton_, &rt60Button_}) {
        button->setBounds(row.removeFromLeft(buttonWidth));
        row.removeFromLeft(az::ui::gap);
    }
    addPaneButton_.setBounds(row.removeFromLeft(editWidth));
    row.removeFromLeft(az::ui::gap);
    removePaneButton_.setBounds(row);
}

void MainComponentPaneSelector::syncTo(const std::vector<rta::view::PaneView>& present) {
    using rta::view::PaneView;
    const auto has = [&present](PaneView view) {
        for (const auto v : present) {
            if (v == view) return true;
        }
        return false;
    };
    // PR #45 fix round 3: every button, XOVER included, must be synced here;
    // a missing line once left XOVER unlit after opening a saved session.
    rtaButton_.setToggleState(has(PaneView::Rta), juce::dontSendNotification);
    transferButton_.setToggleState(has(PaneView::Transfer), juce::dontSendNotification);
    splButton_.setToggleState(has(PaneView::Spl), juce::dontSendNotification);
    xoverButton_.setToggleState(has(PaneView::Xover), juce::dontSendNotification);
    eqButton_.setToggleState(has(PaneView::Eq), juce::dontSendNotification);
    alignButton_.setToggleState(has(PaneView::Align), juce::dontSendNotification);
    rt60Button_.setToggleState(has(PaneView::Rt60), juce::dontSendNotification);
}

void MainComponentPaneSelector::syncEdit(bool addArmed, bool addEnabled, bool removeEnabled) {
    addPaneButton_.setToggleState(addArmed, juce::dontSendNotification);
    addPaneButton_.setEnabled(addEnabled);
    removePaneButton_.setEnabled(removeEnabled);
}
