// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Task B7 (record §6, §7): the routing
// matrix, composed from az_ui's generic GridPanel (project CLAUDE.md,
// "Module boundaries" -- this is the file that KNOWS what a cell means;
// GridPanel never does).
#pragma once

#include <az_ui/az_ui.h>

#include "measure/RoutingPlan.h"
#include "measure/Snapshot.h"
#include "rta/platform/ChannelConfig.h"

#include <span>

namespace rta::view {

/// One row per input channel: a ROLE column (Unused/Measurement/Reference)
/// and, beside it, an AVG column stating whether that channel's route is
/// currently a member of the live spatial-average group -- station-4 fix F3
/// (docs/dsp/2026-09-06-multichannel-l6b.md §6): a route naming a different
/// reference than the group's is refused, and that refusal must be VISIBLE
/// here, not a row that silently looks the same as an ordinary member's.
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

    /// Redraws the AVG column from a published `Snapshot`'s own
    /// `positions` -- `positions[i]` describes `plan.routes[i]`, same
    /// index, same order (`mergeRoutePositions`'s own guarantee,
    /// measure/AnalysisPublish.cpp), so this is a single zipped pass, never
    /// a search by `tfIndex` (which record §6 states is a grouping tag, not
    /// a key unique per route). A channel named by no route in `plan`
    /// (Unused, Reference, or a Measurement channel with no reference to
    /// pair with) reads as "not applicable", not as "excluded" -- those are
    /// different facts. Sized defensively against `plan`/`positions` going
    /// out of step with each other or with `channelCount_` (a routing
    /// change mid-poll, say): never reads past either span, never past a
    /// grid row.
    void updateMembership(const rta::measure::RoutingPlan& plan,
                          std::span<const rta::measure::PositionSummary> positions);

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
