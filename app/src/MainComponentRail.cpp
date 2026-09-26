// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See MainComponentRail.h's own header comment.
#include "MainComponentRail.h"

#include <az_ui/az_ui.h>

namespace {

// Tall enough for DevicePanel's own content (caption, four field rows, the
// start/stop switch and the fault line -- see DevicePanel::resized()) with a
// little breathing room below rather than a height computed to the pixel:
// the panel fills whatever it is given and a hairline of empty panel face at
// the bottom costs nothing.
constexpr int kDevicePanelHeight = 300;

// Task F2: az::ui::GridPanel divides whatever bounds it is given across its
// header row plus rta::measure::kMaxTransferFunctions (8) data rows -- it
// has no minimum-row-height floor of its own (GridPanel.h's own class
// comment: "geometry only"), so this is a flat pixel budget rather than a
// per-row metric multiplied out: comfortably readable for 9 rows (header +
// 8 channels) without crowding channelRoleTable_ below it out of the rail.
constexpr int kRoutingMatrixHeight = 220;

// Fix round item 4: header + ~4 rows -- enough for channelRoleTable_ to be
// worth looking at without dominating the rail. Its own ListBox scrolls
// internally past this if there are more channels than fit.
constexpr int kChannelRoleTableMinHeight = 150;

}  // namespace

MainComponentRail::MainComponentRail(rta::platform::AudioIo& audioIo, int channelCount)
    : devicePanel_(audioIo),
      channelRoleTable_(audioIo.bus().config()),
      routingMatrix_(audioIo.bus().config(), channelCount) {}

void MainComponentRail::attachTo(juce::Component& parent) {
    railScrollContent_.addAndMakeVisible(devicePanel_);
    railScrollContent_.addAndMakeVisible(channelRoleTable_);
    railScrollContent_.addAndMakeVisible(routingMatrix_);
    railScrollView_.setViewedComponent(&railScrollContent_, false);
    parent.addAndMakeVisible(railScrollView_);
}

void MainComponentRail::layout(juce::Rectangle<int> bounds) {
    // Fix round PRs #36/#37 item 4: devicePanel_ (kDevicePanelHeight) +
    // routingMatrix_ (kRoutingMatrixHeight) + channelRoleTable_ used to
    // share whatever was left of `bounds` -- fine at a tall window, but at
    // 1280x800 (and worse at 1100x760) that leftover ran out INSIDE
    // devicePanel_'s own budget: routingMatrix_ was clamped to a sliver of
    // its 220px (its GridPanel divides whatever height it gets across 9
    // rows with no minimum -- kRoutingMatrixHeight's own comment -- so the
    // rows' text overlapped each other) and channelRoleTable_ was left
    // exactly zero pixels (clipped away entirely). Both widgets show a ROLE
    // column, which is what made the squeezed routing matrix read as "the
    // channel-role table" in review.
    //
    // Fix: all three now live inside railScrollContent_, sized to their
    // combined NATURAL height (never less), and railScrollView_ scrolls
    // that content within `bounds` -- so all three always get their full,
    // non-degenerate size, and a short window scrolls instead of squeezing
    // one of them to nothing. Pinned by app/tests_juce/
    // test_main_component_rail_layout.cpp (fix round MEDIUM F1).
    const int railContentHeight = kDevicePanelHeight + az::ui::gap * 2 + kRoutingMatrixHeight +
                                  az::ui::gap * 2 + kChannelRoleTableMinHeight;
    railScrollView_.setBounds(bounds);

    // Fix round LOW F4: subtracting the scrollbar's width unconditionally
    // left an empty strip down the right edge of every widget whenever the
    // rail was already tall enough that nothing scrolls. Only reserve it
    // when the content actually overflows `bounds` -- the same comparison
    // `railContentHeight` already exists to make.
    const bool contentOverflows = bounds.getHeight() < railContentHeight;
    const int contentWidth =
        bounds.getWidth() - (contentOverflows ? railScrollView_.getScrollBarThickness() : 0);
    railScrollContent_.setSize(contentWidth, juce::jmax(bounds.getHeight(), railContentHeight));

    auto content = railScrollContent_.getLocalBounds();
    devicePanel_.setBounds(content.removeFromTop(kDevicePanelHeight));
    content.removeFromTop(az::ui::gap * 2);
    // Fixed height for kMaxTransferFunctions rows plus a header row --
    // RoutingMatrix has no dynamic resize the way channelRoleTable_'s
    // ListBox does, so it gets a fixed slice rather than "whatever is left".
    routingMatrix_.setBounds(content.removeFromTop(kRoutingMatrixHeight));
    content.removeFromTop(az::ui::gap * 2);

    // Fills whatever is left of railScrollContent_ -- always at least
    // kChannelRoleTableMinHeight, more when `bounds` itself is taller than
    // the natural stack (railContentHeight above).
    channelRoleTable_.setBounds(content);
}
