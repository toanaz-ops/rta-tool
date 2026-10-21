// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. The seven pane-selector buttons (RTA, TRANSFER,
// SPL, XOVER, EQ, ALIGN, RT60), their radio-group wiring, their layout and
// the toggle-state sync, split out of MainComponent.h (H7 plan T0).
//
// The buttons stay CHILDREN of MainComponent -- `attach()` adds them to the
// parent in the same order the old inline code did -- so the component-tree
// order and every `findButtonByText` lookup are unchanged. This class owns
// the buttons, not the screen position or the pane workspace: what a click
// DOES is the callback `attach()` receives (MainComponent::selectPaneView).
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "view/PaneSelectorDecision.h"

#include <functional>

class MainComponentPaneSelector {
public:
    using SelectFn = std::function<void(rta::view::PaneSelectorButton)>;

    /// Wires the radio group, hints and click handlers, marks RTA selected
    /// (it matches `MainComponent::currentPaneView_`'s own default), then adds
    /// the seven buttons to `parent`. `onSelect` is called from a click.
    void attach(juce::Component& parent, SelectFn onSelect);

    /// Seven equal cells with `az::ui::gap` between them, filling `row`.
    void layout(juce::Rectangle<int> row);

    /// Lights exactly the button that matches `view`, without notification.
    void syncTo(rta::view::PaneView view);

    /// The text STORE labels a stored trace's group with: the button's own
    /// string, never a second literal (MainComponentStore.cpp, T7).
    [[nodiscard]] juce::String rtaLabel() const { return rtaButton_.getButtonText(); }
    [[nodiscard]] juce::String transferLabel() const { return transferButton_.getButtonText(); }

private:
    juce::TextButton rtaButton_{"RTA"};
    juce::TextButton transferButton_{"TRANSFER"};
    juce::TextButton splButton_{"SPL"};
    juce::TextButton xoverButton_{"XOVER"};  // ALIGN-R8 reversal: G18 live pane
    juce::TextButton eqButton_{"EQ"};        // L7-EQ UI lane: the auto-EQ pane
    juce::TextButton alignButton_{"ALIGN"};  // ALIGN-UI lane: the alignment wizard pane
    juce::TextButton rt60Button_{"RT60"};    // RT60-UI lane: the reverberation-time table
};
