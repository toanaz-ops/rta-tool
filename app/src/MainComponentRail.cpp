// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See MainComponentRail.h's own header comment.
#include "MainComponentRail.h"

#include "TfSlotPicker.h"

#include <az_ui/az_ui.h>

namespace {

// Tall enough for DevicePanel's own content (caption, four field rows, the
// start/stop switch and the fault line -- see DevicePanel::resized()) with a
// little breathing room below rather than a height computed to the pixel:
// the panel fills whatever it is given and a hairline of empty panel face at
// the bottom costs nothing.
constexpr int kDevicePanelHeight = 300;

// Task F2: a flat pixel budget for routingMatrix_, comfortably readable for
// a header plus 8 rows without crowding channelRoleTable_ below it out of the
// rail. It is FIXED however many channels the device has (D1, lane H2):
// RoutingMatrix scrolls its own rows inside this slot (its viewport), so 64
// inputs cost scrolling, not rail height -- and no other widget's position
// depends on the device.
constexpr int kRoutingMatrixHeight = 220;

// Fix round item 4: header + ~4 rows -- enough for channelRoleTable_ to be
// worth looking at without dominating the rail. Its own ListBox scrolls
// internally past this if there are more channels than fit.
constexpr int kChannelRoleTableMinHeight = 150;

// H8 lane a: one row for the TF slot picker, between devicePanel_ and the
// routing matrix it configures.
constexpr int kTfSlotPickerHeight = 26;

}  // namespace

MainComponentRail::MainComponentRail(rta::platform::AudioIo& audioIo, int channelCount,
                                     std::function<rta::platform::Fault()> analysisFault)
    : devicePanel_(audioIo, std::move(analysisFault)),
      channelRoleTable_(audioIo.bus().config()),
      routingMatrix_(audioIo.bus().config(), channelCount) {
    // Combo ids are 1-based (0 means "nothing selected"), slot = id - 1.
    for (int slot = 0; slot < rta::measure::kMaxTransferFunctions; ++slot) {
        tfSlotPicker_.addItem("TF " + juce::String(slot + 1), slot + 1);
    }
    tfSlotPicker_.setSelectedId(routingMatrix_.activeTransferFunction() + 1, juce::dontSendNotification);
    tfSlotPicker_.getProperties().set(az::ui::hintProperty,
                                      "which transfer function the next MEAS/REF click assigns a channel to");
    tfSlotPicker_.onChange = [this] {
        // Id 0 means "nothing selected" (a cleared combo): tfSlotForPickerId
        // clamps, so the slot is never -1 (H8 review L-8).
        routingMatrix_.setActiveTransferFunction(rta::app::tfSlotForPickerId(tfSlotPicker_.getSelectedId()));
    };
}

void MainComponentRail::attachTo(juce::Component& parent) {
    // HW-190 (owner decision 2026-11-09): the DEVICE block (START/STOP above
    // all) is a child of `parent`, NOT of the scrolled content, so no scroll
    // position and no row above it can ever push it out of sight.
    parent.addAndMakeVisible(devicePanel_);
    railScrollContent_.addAndMakeVisible(channelRoleTable_);
    railScrollContent_.addAndMakeVisible(routingMatrix_);
    railScrollContent_.addAndMakeVisible(tfSlotPicker_);
    railScrollView_.setViewedComponent(&railScrollContent_, false);
    parent.addAndMakeVisible(railScrollView_);
}

int MainComponentRail::devicePanelHeight() noexcept { return kDevicePanelHeight; }

void MainComponentRail::layoutDevicePanel(juce::Rectangle<int>& column) {
    devicePanel_.setBounds(column.removeFromTop(kDevicePanelHeight));
    column.removeFromTop(az::ui::gap);
}

juce::Rectangle<int> MainComponentRail::layout(juce::Rectangle<int> bounds, int topRowsHeight) {
    // Fix round PRs #36/#37 item 4: routingMatrix_ (kRoutingMatrixHeight) and
    // channelRoleTable_ used to share whatever was left of `bounds` -- fine at
    // a tall window, but at 1280x800 that leftover ran out and routingMatrix_
    // was clamped to a sliver (its GridPanel divides whatever height it gets
    // across 9 rows with no minimum, so the rows' text overlapped) and
    // channelRoleTable_ was left zero pixels. Fix: everything below the pinned
    // device block lives inside railScrollContent_, sized to its combined
    // NATURAL height (never less), and railScrollView_ scrolls it within
    // `bounds`. Pinned by app/tests_juce/test_main_component_rail_layout.cpp.
    //
    // HW-190: the device block left the scroll area (layoutDevicePanel), and
    // the button rows that used to sit ABOVE the viewport moved INTO it, as
    // its first `topRowsHeight` pixels. Pinning the device block alone would
    // have taken 300 px from a viewport that is only 168 px tall at 1300x698,
    // leaving the routing matrix and role table unreachable at every size.
    const int railContentHeight = topRowsHeight + kTfSlotPickerHeight +
                                  az::ui::gap + kRoutingMatrixHeight +
                                  az::ui::gap * 2 + kChannelRoleTableMinHeight;
    railScrollView_.setBounds(bounds);

    // Fix round LOW F4: only reserve the scrollbar's width when the content
    // actually overflows `bounds`.
    const bool contentOverflows = bounds.getHeight() < railContentHeight;
    const int contentWidth =
        bounds.getWidth() - (contentOverflows ? railScrollView_.getScrollBarThickness() : 0);
    railScrollContent_.setSize(contentWidth, juce::jmax(bounds.getHeight(), railContentHeight));

    auto content = railScrollContent_.getLocalBounds();
    const auto rows = content.removeFromTop(topRowsHeight);
    tfSlotPicker_.setBounds(content.removeFromTop(kTfSlotPickerHeight));
    content.removeFromTop(az::ui::gap);
    // Fixed slice, not "whatever is left": the rows inside scroll (see
    // kRoutingMatrixHeight), so this height does not depend on channel count.
    routingMatrix_.setBounds(content.removeFromTop(kRoutingMatrixHeight));
    content.removeFromTop(az::ui::gap * 2);

    // Fills whatever is left of railScrollContent_ -- always at least
    // kChannelRoleTableMinHeight, more when `bounds` itself is taller.
    channelRoleTable_.setBounds(content);
    return rows;
}
