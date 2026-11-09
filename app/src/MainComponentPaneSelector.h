// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. The eight pane-selector buttons (RTA, TRANSFER,
// SPL, XOVER, EQ, ALIGN, RT60, IR), the two pane-edit buttons (+ PANE, - PANE),
// their layout and the toggle-state sync, split out of MainComponent.h (H7 plan T0).
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
#include <vector>

class MainComponentPaneSelector {
public:
    using SelectFn = std::function<void(rta::view::PaneSelectorButton)>;

    /// Wires hints and click handlers, marks RTA lit (it matches
    /// `MainComponent::currentPaneView_`'s own default), then adds the eight
    /// view buttons and `+ PANE` / `- PANE` to `parent`. `onSelect` is called
    /// from a view click, `onToggleAdd` from `+ PANE`, `onRemoveLast` from
    /// `- PANE`.
    void attach(juce::Component& parent, SelectFn onSelect, std::function<void()> onToggleAdd,
                std::function<void()> onRemoveLast);

    /// Eight view cells then the two narrower pane-edit cells, `az::ui::gap`
    /// between them, filling `row`.
    void layout(juce::Rectangle<int> row);

    /// Lights every button whose view is in `present` (a multi-pane workspace
    /// shows several), without notification. There is no radio group: that
    /// mutual exclusion would un-light all but one of them.
    void syncTo(const std::vector<rta::view::PaneView>& present);

    /// `+ PANE` armed state (lit) and the two edit buttons' enable state.
    void syncEdit(bool addArmed, bool addEnabled, bool removeEnabled);

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
    juce::TextButton irButton_{"IR"};        // H9 / L4c: the impulse-response gate
    juce::TextButton addPaneButton_{"+ PANE"};     // arm: the next view press appends
    juce::TextButton removePaneButton_{"- PANE"};  // drops the last pane
};
