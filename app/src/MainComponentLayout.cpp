// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. MainComponent.cpp's own 400-line-cap split
// (fix round PRs #36/#37 item 3, same shape as MainComponentDelay.cpp /
// MainComponentCalibration.cpp / MainComponentSpl.cpp / MainComponentPanes.cpp):
// paint() and resized(), the drawing/layout half of the composition root.
// Pure relocation from MainComponent.cpp -- no behaviour changed here; the
// rail-overflow fix (item 4) is its own, separate change to this file.
#include "MainComponent.h"

namespace {

// The legend gutter's own column, per Metrics.h: every panel that shares a
// legend column starts its fields at az::ui::gutterWidth, but that figure
// sizes a FIELD's legend, not a whole rail's width. This is the rail width
// itself -- wide enough for DevicePanel's widest row (a device name combo
// plus its gutter) and for ChannelRoleTable's index/name/role columns
// without either one clipping.
constexpr int kRailWidth = 360;

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

}  // namespace

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(az::ui::background);

    // Same two-piece masthead SpecimenComponent draws: the wordmark and its
    // qualifier want different weights, and a one-piece title needs a
    // non-ASCII dash, which is a source-encoding question this project has
    // no reason to open.
    auto masthead = mastheadArea_;
    auto rule = masthead.removeFromBottom(2);

    const auto brandFont = az::ui::legendFont(az::ui::switchFontSize, true, az::ui::trackingCaption);
    g.setColour(az::ui::text);
    g.setFont(brandFont);
    g.drawText("RTA TOOL", masthead, juce::Justification::centredLeft);

    masthead.removeFromLeft(static_cast<int>(az::ui::stringWidth(brandFont, "RTA TOOL")) + az::ui::gap * 3);
    g.setColour(az::ui::dim);
    g.setFont(az::ui::legendFont(az::ui::columnFontSize, false, az::ui::trackingColumn));
    g.drawText("REAL-TIME ANALYSER", masthead, juce::Justification::centredLeft);

    az::ui::drawEngravedDivider(g, rule);
}

void MainComponent::resized() {
    // Station-4 fix F3: also called here, not only from the 2 Hz timer --
    // see refreshMembershipFromSnapshot()'s own comment for why a caller
    // driving this class with no message loop pumped (tools/snapshot.cpp)
    // would otherwise never see the AVG column populated at all.
    refreshMembershipFromSnapshot();

    auto area = getLocalBounds().reduced(az::ui::gap * 2);

    mastheadArea_ = area.removeFromTop(az::ui::transportHeight / 2);
    area.removeFromTop(az::ui::gap * 2);

    auto rail = area.removeFromLeft(kRailWidth);
    area.removeFromLeft(az::ui::gap * 2);

    modeSwitch_.setBounds(rail.removeFromTop(az::ui::buttonCellHeight));
    rail.removeFromTop(az::ui::gap * 2);

    // L7-DELAY task F2: one fixed row -- LOCATE, APPLY, and the readout
    // sharing what's left of the row's width.
    auto locateRow = rail.removeFromTop(az::ui::buttonCellHeight);
    const int buttonWidth = (locateRow.getWidth() - az::ui::gap * 2) / 3;
    locateButton_.setBounds(locateRow.removeFromLeft(buttonWidth));
    locateRow.removeFromLeft(az::ui::gap);
    applyButton_.setBounds(locateRow.removeFromLeft(buttonWidth));
    locateRow.removeFromLeft(az::ui::gap);
    delayReadout_.setBounds(locateRow);
    rail.removeFromTop(az::ui::gap * 2);

    // L6a Wave 3: one more fixed row, same shape as the Locate row above --
    // CAL START, CAL END, and the readout sharing what's left.
    auto calibrationRow = rail.removeFromTop(az::ui::buttonCellHeight);
    const int calibrationButtonWidth = (calibrationRow.getWidth() - az::ui::gap * 2) / 3;
    calibrationStartButton_.setBounds(calibrationRow.removeFromLeft(calibrationButtonWidth));
    calibrationRow.removeFromLeft(az::ui::gap);
    calibrationEndButton_.setBounds(calibrationRow.removeFromLeft(calibrationButtonWidth));
    calibrationRow.removeFromLeft(az::ui::gap);
    calibrationReadout_.setBounds(calibrationRow);
    rail.removeFromTop(az::ui::gap * 2);

    // Task W2-E2b part B: one more fixed row -- the export button and its
    // readout sharing what's left, the same shape as the two rows above.
    auto exportRow = rail.removeFromTop(az::ui::buttonCellHeight);
    // "EXPORT REPORT" is wider text than "CAL START"/"CAL END" above it, so
    // this row's button gets a bigger share (2/5 rather than that row's 1/3)
    // -- otherwise the label truncates (measured against main-live.png).
    const int exportButtonWidth = (exportRow.getWidth() - az::ui::gap) * 2 / 5;
    exportReportButton_.setBounds(exportRow.removeFromLeft(exportButtonWidth));
    exportRow.removeFromLeft(az::ui::gap);
    exportReportReadout_.setBounds(exportRow);
    rail.removeFromTop(az::ui::gap * 2);

    // Fix round PRs #36/#37 item 4: devicePanel_ (kDevicePanelHeight) +
    // routingMatrix_ (kRoutingMatrixHeight) + channelRoleTable_ used to share
    // whatever was left of `rail` after the three button rows above -- fine
    // at a tall window, but at 1280x800 (and worse at 1100x760) that
    // leftover ran out INSIDE devicePanel_'s own budget: routingMatrix_ was
    // clamped to a sliver of its 220px (its GridPanel divides whatever
    // height it gets across 9 rows with no minimum -- see kRoutingMatrixHeight's
    // own comment -- so the rows' text overlapped each other) and
    // channelRoleTable_ was left exactly zero pixels (clipped away
    // entirely). Both widgets show a ROLE column, which is what made the
    // squeezed routing matrix read as "the channel-role table" in review.
    //
    // Fix: devicePanel_/routingMatrix_/channelRoleTable_ now live inside
    // railScrollContent_, sized to their combined NATURAL height (never
    // less), and railScrollView_ scrolls that content within `rail` -- so
    // all three always get their full, non-degenerate size, and a short
    // window scrolls instead of squeezing one of them to nothing.
    constexpr int kChannelRoleTableMinHeight = 150;  // header + ~4 rows: enough to read, not to dominate
    const int railContentHeight = kDevicePanelHeight + az::ui::gap * 2 + kRoutingMatrixHeight +
                                  az::ui::gap * 2 + kChannelRoleTableMinHeight;
    railScrollView_.setBounds(rail);
    const int contentWidth = rail.getWidth() - railScrollView_.getScrollBarThickness();
    railScrollContent_.setSize(contentWidth, juce::jmax(rail.getHeight(), railContentHeight));

    auto content = railScrollContent_.getLocalBounds();
    devicePanel_.setBounds(content.removeFromTop(kDevicePanelHeight));
    content.removeFromTop(az::ui::gap * 2);
    // Fixed height for kMaxTransferFunctions rows plus a header row --
    // RoutingMatrix has no dynamic resize the way channelRoleTable_'s
    // ListBox does, so it gets a fixed slice rather than "whatever is left".
    routingMatrix_.setBounds(content.removeFromTop(kRoutingMatrixHeight));
    content.removeFromTop(az::ui::gap * 2);

    // Fills whatever is left of railScrollContent_ -- always at least
    // kChannelRoleTableMinHeight, more when the rail itself is taller than
    // the natural stack (railContentHeight above).
    channelRoleTable_.setBounds(content);

    // Owner decision 2026-09-26: RTA/TRANSFER/SPL sits above the workspace,
    // in the main content area -- not the rail, which is already full.
    auto paneSelectorRow = area.removeFromTop(az::ui::buttonCellHeight);
    area.removeFromTop(az::ui::gap * 2);
    layoutPaneSelectorRow(paneSelectorRow);

    workspace_->setBounds(area);
}
