// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. The per-plot MTW/Fixed control
// docs/reports/005-mtw-engine.md "Known gaps" names as missing: "the
// per-plot source toggle has no UI control yet -- it exists as an API the
// tests and a future control can call."
//
// Lives in app/, not ui/: az_ui carries no measurement vocabulary (project
// CLAUDE.md's module-boundary section), and "MTW" / "Fixed" -- which engine
// feeds this plot -- is exactly that vocabulary. This file is built entirely
// from plain juce::TextButton, the same primitive DevicePanel's start/stop
// switch already uses; it picks up the SODIUM RACK look from the app-wide
// `az::ui::AzLookAndFeel` set as the JUCE default (MainComponent.cpp /
// tools/snapshot.cpp), with no new colour or token added to az_ui for it.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "view/BodeLayout.h"
#include "view/TransferView.h"

namespace rta::view {

/// Two mutually exclusive buttons, "MTW" and "FIXED", driving one
/// `TransferView` pane's `setSource`. A segmented pair rather than a single
/// checkbox: the record's own default is MTW, and which of two NAMED engines
/// is active is clearer read from "which button is pressed" than from one
/// box's on/off state.
class TransferSourceToggle final : public juce::Component {
public:
    TransferSourceToggle(TransferView& view, TransferPane pane);

    void resized() override;

    /// Places this control at the top-right corner of `pane`'s own
    /// rectangle, `height` tall -- `height` is a caller argument rather than
    /// a constant here because the ribbon pane (34 px, `kRibbonHeight`) is
    /// too short for the same height the magnitude/phase panes use. Clear of
    /// the level labels on the left and never taller than the pane it sits
    /// in, so it cannot bleed into the plot area or a neighbour's own
    /// toggle.
    void placeInPane(PaneRect pane, int height);

    /// Re-reads `view.source(pane)` and updates which button shows pressed.
    /// Called once at construction and after every click; a caller does not
    /// need to call this itself unless something else changed the view's
    /// source for this pane (there is no such call site today, but the
    /// contract does not assume there never will be one).
    void refresh();

    /// Test-only entry points into the two buttons -- the same reasoning
    /// TransferView::panes() states on its own declaration: production API
    /// existing so the click-to-`setSource` wiring is testable directly,
    /// rather than a test synthesizing a mouseDown at a pixel coordinate no
    /// other file commits to.
    ///
    /// Calls `onClick` directly rather than `juce::Button::triggerClick()`:
    /// triggerClick() POSTS an async command message
    /// (juce_Button.cpp -- `postCommandMessage`), delivered only once
    /// something pumps the JUCE message loop, which no test in this
    /// headless target does. A REAL mouse click never goes through that
    /// path -- `mouseUp` calls `internalClickCallback` synchronously -- so
    /// this is the production callback fired the same way a real click
    /// fires it, just without JUCE's own (redundant here: `select()` below
    /// already calls `refresh()`) toggle-state bookkeeping in between.
    void clickMtwButton() { mtwButton_.onClick(); }
    void clickFixedButton() { fixedButton_.onClick(); }

    [[nodiscard]] bool isMtwSelected() const noexcept { return mtwButton_.getToggleState(); }

private:
    void select(TransferSource source);

    TransferView& view_;
    TransferPane pane_;
    juce::TextButton mtwButton_{"MTW"};
    juce::TextButton fixedButton_{"FIXED"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransferSourceToggle)
};

}  // namespace rta::view
