// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Task B7.
#include "view/RoutingMatrix.h"

#include <algorithm>
#include <string>
#include <vector>

namespace rta::view {

namespace {

using rta::platform::ChannelRole;

const char* roleLabel(ChannelRole role) {
    switch (role) {
        case ChannelRole::Unused: return "UNUSED";
        case ChannelRole::Measurement: return "MEAS";
        case ChannelRole::Reference: return "REF";
    }
    return "UNUSED";
}

/// A channel not named by any route in the plan at all -- Unused,
/// Reference, or a Measurement channel `planRouting` could not pair with a
/// reference. Plain ASCII (CLAUDE.md's "Reading out numbers": this project
/// has been bitten by encoding on a read-modify-write before, and an
/// embedded mono face is not guaranteed to carry every Unicode glyph), same
/// placeholder character `Readouts.h::readoutLine` already uses for "no
/// data yet".
constexpr const char* kNotApplicable = "--";

/// AVG column text for one route's `Membership` (measure/Snapshot.h,
/// station-4 fix F3). ASCII only, for the same reason `kNotApplicable` is.
const char* membershipLabel(rta::measure::Membership membership) {
    switch (membership) {
        case rta::measure::Membership::Member: return "AVG";
        case rta::measure::Membership::ExcludedDifferentReference: return "REF!=";
    }
    return kNotApplicable;
}

/// The cycle a click walks: Unused -> Measurement -> Reference -> Unused.
/// This is a DISPLAY policy of the matrix, not a rule ChannelConfig itself
/// states -- the config accepts any role for any channel at any time.
ChannelRole nextRole(ChannelRole role) {
    switch (role) {
        case ChannelRole::Unused: return ChannelRole::Measurement;
        case ChannelRole::Measurement: return ChannelRole::Reference;
        case ChannelRole::Reference: return ChannelRole::Unused;
    }
    return ChannelRole::Unused;
}

}  // namespace

RoutingMatrix::RoutingMatrix(rta::platform::ChannelConfig& config, int channelCount)
    : config_(config), channelCount_(channelCount) {
    addAndMakeVisible(grid_);

    std::vector<std::string> rowHeaders;
    rowHeaders.reserve(static_cast<std::size_t>(channelCount_));
    for (int ch = 0; ch < channelCount_; ++ch) {
        rowHeaders.push_back(std::to_string(ch));
    }
    grid_.setRowHeaders(rowHeaders);
    grid_.setColumnHeaders({"ROLE", "AVG"});
    grid_.setGridSize(channelCount_, 2);
    for (int ch = 0; ch < channelCount_; ++ch) {
        grid_.setCellText(ch, 1, kNotApplicable);
    }
    grid_.onCellClicked = [this](int row, int column) { onCellClicked(row, column); };

    refreshFromConfig();
}

void RoutingMatrix::onCellClicked(int row, int /*column*/) {
    if (row < 0 || row >= channelCount_) return;

    const ChannelRole current = config_.role(row);
    const ChannelRole next = nextRole(current);
    config_.setRole(row, next);

    // Tag the channel with the active transfer function ONLY when the click
    // assigned it a real role -- cycling back to Unused leaves whatever
    // tfIndex it last had (B2's planRouting ignores an Unused channel's
    // tfIndex entirely, so there is nothing to protect by clearing it, and
    // clearing it would lose the assignment the moment an operator cycles
    // through Unused by mistake on the way to a different role).
    if (next != ChannelRole::Unused) {
        config_.setTransferFunction(row, activeTfIndex_);
    }

    refreshFromConfig();
}

void RoutingMatrix::refreshFromConfig() {
    for (int ch = 0; ch < channelCount_; ++ch) {
        grid_.setCellText(ch, 0, roleLabel(config_.role(ch)));
    }
}

void RoutingMatrix::updateMembership(const rta::measure::RoutingPlan& plan,
                                     std::span<const rta::measure::PositionSummary> positions) {
    // Reset every row to "not part of any route" first: a channel a route
    // named LAST poll but does not name this one (a routing change) must
    // not keep showing its stale AVG/REF!= text.
    for (int ch = 0; ch < channelCount_; ++ch) {
        grid_.setCellText(ch, 1, kNotApplicable);
    }

    const std::size_t routeCount = std::min(plan.routes.size(), positions.size());
    for (std::size_t i = 0; i < routeCount; ++i) {
        const int channel = plan.routes[i].measurementChannel;
        if (channel < 0 || channel >= channelCount_) continue;
        grid_.setCellText(channel, 1, membershipLabel(positions[i].membership));
    }
}

void RoutingMatrix::resized() {
    grid_.setBounds(getLocalBounds());
}

}  // namespace rta::view
