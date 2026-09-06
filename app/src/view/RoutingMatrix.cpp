// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Task B7.
#include "view/RoutingMatrix.h"

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
    grid_.setColumnHeaders({"ROLE"});
    grid_.setGridSize(channelCount_, 1);
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

void RoutingMatrix::resized() {
    grid_.setBounds(getLocalBounds());
}

}  // namespace rta::view
