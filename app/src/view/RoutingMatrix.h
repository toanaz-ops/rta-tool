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
#include <string>
#include <vector>

namespace rta::view {

/// One row per input channel (the row COUNT follows the device: see
/// `setChannelNames`, capped at `rta::platform::kMaxChannels`; the columns
/// are fixed). Rows live inside a `juce::Viewport`, so a 64-input interface
/// scrolls inside whatever height the host gives this widget instead of
/// squeezing 64 rows into it (D1/D8, lane H2). Each row is a ROLE column
/// (Unused/Measurement/Reference) and, beside it, an AVG column stating whether that channel's route is
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
    /// Smallest row the grid is drawn at. 22 px keeps `tableFontSize` text
    /// readable, and 8 rows plus the header band (30 + 8 * 22 = 206) still fit
    /// the rail's 220 px slot, so an 8-input interface looks as it always did.
    static constexpr int kMinRowHeight = 22;

    /// `channelCount` is the initial row count (clamped to [0, kMaxChannels]);
    /// rows are labelled by index until `setChannelNames` supplies real names.
    RoutingMatrix(rta::platform::ChannelConfig& config, int channelCount);

    /// Rebuilds the rows to one per name -- `names.size()` rows, capped at
    /// `kMaxChannels` (the size of `ChannelConfig`'s own tables; a row past it
    /// could never carry a role, exactly `ChannelRoleTable::setChannelNames`'s
    /// clip). Row labels are the names (an empty name falls back to the
    /// 1-based channel number, the same numbering `ChannelRoleTable` shows).
    ///
    /// SHRINKING does NOT touch `ChannelConfig`: a role/tf a dropped channel
    /// still holds is left where it is (decision, lane H2). It is harmless
    /// while the channel is off the end -- `planRouting` and
    /// `ChannelConfig::snapshot` both clamp to the channel count the bus
    /// actually has, so no route can name it -- and it is the operator's own
    /// assignment coming back if the same interface is reattached, exactly as
    /// `ChannelRoleTable` (which also keeps roles it no longer lists) behaves.
    /// Clearing here would instead wipe assignments on every transient
    /// "device closed" blip between two sample-rate changes.
    void setChannelNames(std::vector<std::string> names);

    /// Rows currently shown.
    [[nodiscard]] int channelCount() const noexcept { return channelCount_; }

    /// The viewport that scrolls the rows. Test seam (orphan_check TEST HOOK
    /// rule: a `*ForTest` name a test really references), D8.
    [[nodiscard]] const juce::Viewport& viewportForTest() const noexcept { return viewport_; }
    /// Whether the vertical scrollbar is actually on screen right now.
    /// (`Viewport::isVerticalScrollBarShown()` reports the "may show" flag
    /// set in the constructor, so it is true even with nothing to scroll.)
    [[nodiscard]] bool scrollBarVisibleForTest() noexcept {
        return viewport_.getVerticalScrollBar().isVisible();
    }

    /// Height the grid needs to show every row at `kMinRowHeight` (header
    /// band included). Below this the grid scrolls; at or above it, the grid
    /// fills the widget exactly as it did before the rows scrolled.
    [[nodiscard]] int naturalHeight() const noexcept;

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
    void rebuildRows(std::vector<std::string> rowHeaders);
    void layoutGrid();

    rta::platform::ChannelConfig& config_;
    int channelCount_ = 0;
    int activeTfIndex_ = 0;
    // grid_ before viewport_: the viewport holds a raw pointer to it, so the
    // viewport must be destroyed first (reverse declaration order) -- the
    // same rule MainComponentRail.h states for its own content/viewport pair.
    az::ui::GridPanel grid_;
    juce::Viewport viewport_;
};

}  // namespace rta::view
