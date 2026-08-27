// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.5 (Wave D / T9).
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "rta/platform/ChannelConfig.h"

#include <string>
#include <vector>

namespace rta::view {

/// One row per device input channel: its index, its name (middle-elided --
/// `az::ui::elideMiddle` exists precisely because a channel is identified
/// by its LAST characters: "Analogue 1" against "Analogue 11" collide under
/// trailing truncation), and a three-position toggle for its
/// `rta::platform::ChannelRole`. A click anywhere in a row cycles
/// Unused -> Measurement -> Reference -> Unused, writing straight into the
/// `ChannelConfig` the audio callback reads every block.
///
/// This component owns no channel list of its own: `setChannelNames()` is
/// called by whoever owns the `AudioIo` (MainComponent, a later wave)
/// whenever the device's input channel names change. That keeps this table
/// decoupled from `AudioIo` itself -- it only ever touches `ChannelConfig`,
/// which is exactly the seam that lets it be driven from a real device or
/// from a fixed test list with no device at all.
class ChannelRoleTable final : public juce::Component, private juce::ListBoxModel {
public:
    explicit ChannelRoleTable(rta::platform::ChannelConfig& channelConfig);

    /// Replaces the row list. Channel `i`'s role still lives at
    /// `channelConfig[i]` regardless of how many rows are shown here --
    /// shrinking the list (a smaller device opened) does not reset the role
    /// of a channel that is merely off-screen now.
    void setChannelNames(std::vector<std::string> names);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // juce::ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics&, int width, int height,
                           bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;

    rta::platform::ChannelConfig& channelConfig_;
    std::vector<std::string> channelNames_;
    juce::ListBox listBox_{"channel-role-table"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelRoleTable)
};

}  // namespace rta::view
