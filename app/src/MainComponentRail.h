// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. Fix round PR #40 LOW F3: MainComponent.h had
// zero headroom left under the 400-line cap (item 3 of the previous round
// moved paint()/resized() out; this round needs a REAL move, not squeezed
// comments). devicePanel_/channelRoleTable_/routingMatrix_ and the
// scrolling viewport that keeps all three at their full natural size
// (fix round item 4, MEDIUM F1's own test) are grouped here as one
// MainComponent member instead of five, with the viewport wiring and the
// item-4 layout math that used to live inline in MainComponentLayout.cpp.
//
// This is NOT a reusable widget in the `ui/` sense (project CLAUDE.md's
// module boundaries) -- it is the composition root's own left-rail
// grouping, still measurement-vocabulary-heavy (DevicePanel, ChannelRoleTable,
// RoutingMatrix), and lives in `app/` for exactly that reason.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "measure/RoutingPlan.h"
#include "measure/Snapshot.h"
#include "rta/platform/AudioIo.h"
#include "view/ChannelRoleTable.h"
#include "view/DevicePanel.h"
#include "view/RoutingMatrix.h"

#include <span>
#include <string>
#include <vector>

/// Owns the three widgets MainComponent's left rail below the button rows
/// shows, plus the juce::Viewport that scrolls them (fix round item 4):
/// devicePanel_ / routingMatrix_ / channelRoleTable_ keep their full natural
/// size always -- a short window scrolls instead of squeezing one of them
/// to a degenerate height (MainComponentRail.cpp's own `layout()` comment
/// has the numbers). MainComponent owns exactly one of these; every method
/// here is what MainComponent used to call directly on the three widgets.
class MainComponentRail final {
public:
    MainComponentRail(rta::platform::AudioIo& audioIo, int channelCount);

    /// Adds the viewport (the only child MainComponent itself sees) into
    /// `parent` and wires the three widgets as the viewport content's own
    /// children. Called once, from MainComponent's constructor.
    void attachTo(juce::Component& parent);

    /// The item-4 layout: scrolls devicePanel_/routingMatrix_/channelRoleTable_
    /// within `bounds` (MainComponent's own rail column).
    void layout(juce::Rectangle<int> bounds);

    void setDevicePanelEnabled(bool enabled) { devicePanel_.setEnabled(enabled); }
    void setChannelNames(std::vector<std::string> names) {
        channelRoleTable_.setChannelNames(std::move(names));
    }
    void refreshFromConfig() { routingMatrix_.refreshFromConfig(); }
    void updateMembership(const rta::measure::RoutingPlan& plan,
                          std::span<const rta::measure::PositionSummary> positions) {
        routingMatrix_.updateMembership(plan, positions);
    }

private:
    friend struct MainComponentTestAccess;  // fix round MEDIUM F1: the rail-layout test
    [[nodiscard]] const juce::Component& channelRoleTableForTest() const noexcept { return channelRoleTable_; }
    [[nodiscard]] const juce::Component& routingMatrixForTest() const noexcept { return routingMatrix_; }

    rta::view::DevicePanel devicePanel_;
    rta::view::ChannelRoleTable channelRoleTable_;
    rta::view::RoutingMatrix routingMatrix_;

    // Content declared BEFORE viewport (same trap T-1 rule MainComponent.h's
    // class comment states): reverse destruction unwinds the viewport
    // first, while its unowned target still lives.
    juce::Component railScrollContent_;
    juce::Viewport railScrollView_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponentRail)
};
