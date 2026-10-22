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

// Measurement rows are touch-target high (44), not buttonCellHeight (62): at
// 1280x800 the rows above the device panel must leave it its natural ~260 px
// (device combo, START/STOP, fault line) without scrolling -- pinned by
// test_main_component_rail_layout.cpp. The mode switch keeps its full height.
constexpr int kRowHeight = az::ui::touchTarget;
// Masthead width kept free for "RTA TOOL  REAL-TIME ANALYSER".
constexpr int kBrandReserve = 340;

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

    // H7 generator: GEN / - / + and its readout live in the masthead's empty
    // right side, not the rail. STOP is therefore on screen in every pane at
    // every window size (a show-safety control must never scroll away), and
    // the rail keeps its height for the device panel. The left kBrandReserve
    // px stay clear for the wordmark painted in paint().
    auto strip = mastheadArea_.withTrimmedBottom(az::ui::spacing);  // clear of the engraved rule
    strip.removeFromLeft(juce::jmin(kBrandReserve, strip.getWidth() / 2));
    generator_.layout(strip);

    auto rail = area.removeFromLeft(kRailWidth);
    area.removeFromLeft(az::ui::gap * 2);

    modeSwitch_.setBounds(rail.removeFromTop(az::ui::buttonCellHeight));
    rail.removeFromTop(az::ui::gap);

    // L7-DELAY task F2: one fixed row -- LOCATE, APPLY, and the readout
    // sharing what's left of the row's width.
    auto locateRow = rail.removeFromTop(kRowHeight);
    const int buttonWidth = (locateRow.getWidth() - az::ui::gap * 2) / 3;
    locateButton_.setBounds(locateRow.removeFromLeft(buttonWidth));
    locateRow.removeFromLeft(az::ui::gap);
    applyButton_.setBounds(locateRow.removeFromLeft(buttonWidth));
    locateRow.removeFromLeft(az::ui::gap);
    delayReadout_.setBounds(locateRow);
    rail.removeFromTop(az::ui::gap);

    // SWEEP (H4 T6, decision 1): its own row directly under LOCATE's, same
    // shape as the STORE row below.
    sweep_.layout(rail.removeFromTop(kRowHeight));
    rail.removeFromTop(az::ui::gap);

    // L6a Wave 3: one more fixed row, same shape as the Locate row above --
    // CAL START, CAL END, and the readout sharing what's left.
    auto calibrationRow = rail.removeFromTop(kRowHeight);
    const int calibrationButtonWidth = (calibrationRow.getWidth() - az::ui::gap * 2) / 3;
    calibrationStartButton_.setBounds(calibrationRow.removeFromLeft(calibrationButtonWidth));
    calibrationRow.removeFromLeft(az::ui::gap);
    calibrationEndButton_.setBounds(calibrationRow.removeFromLeft(calibrationButtonWidth));
    calibrationRow.removeFromLeft(az::ui::gap);
    calibrationReadout_.setBounds(calibrationRow);
    rail.removeFromTop(az::ui::gap);

    // Task W2-E2b part B: one more fixed row -- the export button and its
    // readout sharing what's left, the same shape as the two rows above.
    auto exportRow = rail.removeFromTop(kRowHeight);
    // "EXPORT REPORT" is wider text than "CAL START"/"CAL END" above it, so
    // this row's button gets a bigger share (2/5 rather than that row's 1/3)
    // -- otherwise the label truncates (measured against main-live.png).
    const int exportButtonWidth = (exportRow.getWidth() - az::ui::gap) * 2 / 5;
    exportReportButton_.setBounds(exportRow.removeFromLeft(exportButtonWidth));
    exportRow.removeFromLeft(az::ui::gap);
    exportReportReadout_.setBounds(exportRow);
    rail.removeFromTop(az::ui::gap);

    // station-3 STORE (T5): one more fixed row, same shape as the export row
    // above -- the button and its readout sharing what's left.
    auto storeRow = rail.removeFromTop(kRowHeight);
    const int storeButtonWidth = (storeRow.getWidth() - az::ui::gap) / 4;
    storeButton_.setBounds(storeRow.removeFromLeft(storeButtonWidth));
    storeRow.removeFromLeft(az::ui::gap);
    storeReadout_.setBounds(storeRow);
    rail.removeFromTop(az::ui::gap);

    // Session persistence: SAVE SESSION / OPEN SESSION share one row, the
    // readout takes the row below it -- see MainComponentSession::layout.
    auto sessionArea = rail.removeFromTop(kRowHeight * 2 + az::ui::gap);
    session_.layout(sessionArea);
    rail.removeFromTop(az::ui::gap);

    // Fix round item 3 (MEDIUM F3 follow-up): device panel / routing matrix /
    // channel role table and their scroll viewport (item 4) are grouped into
    // rail_ (MainComponentRail.h/.cpp) -- see that file's own layout()
    // comment for the item-4 fix itself.
    rail_.layout(rail);

    // Owner decision 2026-09-26: RTA/TRANSFER/SPL sits above the workspace,
    // in the main content area -- not the rail, which is already full.
    auto paneSelectorRow = area.removeFromTop(az::ui::buttonCellHeight);
    area.removeFromTop(az::ui::gap * 2);
    paneSelector_.layout(paneSelectorRow);

    workspace_->setBounds(area);
}
