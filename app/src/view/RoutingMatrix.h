// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Task B7 (record §6, §7): the routing
// matrix, composed from az_ui's generic GridPanel (project CLAUDE.md,
// "Module boundaries" -- this is the file that KNOWS what a cell means;
// GridPanel never does).
#pragma once

#include <az_ui/az_ui.h>

#include "rta/platform/ChannelConfig.h"

namespace rta::view {

/// One row per input channel, one column showing that channel's role.
/// Clicking a cell cycles Unused -> Measurement -> Reference -> Unused
/// (`ChannelConfig::setRole`) and, when the new role is not Unused, tags
/// the channel with the currently selected transfer-function index
/// (`ChannelConfig::setTransferFunction`, task B1) -- the two per-channel
/// facts a route (task B2's `planRouting`) is resolved from.
class RoutingMatrix final : public juce::Component {
public:
    RoutingMatrix(rta::platform::ChannelConfig& config, int channelCount);

    /// Which transfer-function index a click that ASSIGNS a role (Unused ->
    /// Measurement/Reference) tags the channel with. Does not affect a
    /// channel already routed -- see `onCellClicked`'s own comment.
    void setActiveTransferFunction(int tfIndex) noexcept { activeTfIndex_ = tfIndex; }
    [[nodiscard]] int activeTransferFunction() const noexcept { return activeTfIndex_; }

    /// Re-reads every channel's current role/tfIndex from `config_` and
    /// updates the grid's cell text -- production API so a test (or a
    /// caller reacting to a routing change made elsewhere) can force a
    /// redraw without waiting on a timer.
    void refreshFromConfig();

    void resized() override;

    [[nodiscard]] az::ui::GridPanel& grid() noexcept { return grid_; }

private:
    void onCellClicked(int row, int column);

    rta::platform::ChannelConfig& config_;
    int channelCount_;
    int activeTfIndex_ = 0;
    az::ui::GridPanel grid_;
};

}  // namespace rta::view
