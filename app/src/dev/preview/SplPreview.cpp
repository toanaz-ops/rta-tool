// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/dev/preview.
// Lane L6a task W2-D (record docs/dsp/2026-09-16-spl-pro-l6a.md §4, §11).
#include "dev/preview/SplPreview.h"

#include "dev/preview/PreviewFurniture.h"
#include "view/MeasureColours.h"
#include "view/SplStrip.h"

#include <az_ui/az_ui.h>

#include <cmath>

namespace {

constexpr std::uint32_t kBlockSamples = 48000;  ///< 1 s blocks
constexpr std::size_t kBlockCount = 600;         ///< 10 minutes at 1 s/block

/// A fixed, deterministic level-over-time signal -- no microphone, no RNG:
/// two slow cycles plus a single loud excursion, so the picture exercises
/// both the ordinary sweep of the trace and a level near the top of the
/// pane, the same reasoning PhaseAlignPreview.cpp gives for its own fixed
/// constants.
std::vector<rta::meter::Block> makeCannedBlocks() {
    std::vector<rta::meter::Block> blocks;
    blocks.reserve(kBlockCount);
    for (std::size_t i = 0; i < kBlockCount; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kBlockCount);
        double levelDb = 78.0 + 12.0 * std::sin(2.0 * 3.14159265358979 * 2.0 * t);
        if (i > kBlockCount / 2 && i < kBlockCount / 2 + 15) levelDb = 118.0;  // one loud excursion
        rta::meter::Block b;
        b.blockIndex = i;
        b.blockSamples = kBlockSamples;
        b.sumSquares = static_cast<double>(kBlockSamples) * std::pow(10.0, levelDb / 10.0);
        blocks.push_back(b);
    }
    return blocks;
}

}  // namespace

SplPreview::SplPreview() : blocks_(makeCannedBlocks()) {}

void SplPreview::resized() {
    auto area = getLocalBounds().reduced(az::ui::gap * 2);
    mastheadArea_ = area.removeFromTop(az::ui::transportHeight / 2);
    area.removeFromTop(az::ui::gap * 2);
    stripArea_ = area;
}

void SplPreview::paint(juce::Graphics& g) {
    g.fillAll(az::ui::background);
    rta::dev::preview::drawMasthead(g, mastheadArea_, "SPL",
                                     "EARLY PREVIEW  --  SYNTHETIC 10-MINUTE STRIP");

    const rta::view::SplStripRect content{ stripArea_.getX(), stripArea_.getY(),
                                          stripArea_.getWidth(), stripArea_.getHeight() };
    const auto geometry =
        rta::view::splStripGeometry(content, blocks_.front().blockIndex, blocks_.back().blockIndex,
                                    140.0, 40.0);

    // Gridlines every 20 dB -- the same hairline convention PlotAxes.h draws
    // for the RTA plot's own dB grid, reused here rather than reinvented.
    g.setColour(rta::view::grid);
    for (double db = 40.0; db <= 140.0; db += 20.0) {
        const float y = geometry.yForDb(db);
        g.drawHorizontalLine(static_cast<int>(y), static_cast<float>(stripArea_.getX()),
                             static_cast<float>(stripArea_.getRight()));
    }

    const auto points = rta::view::splStripPoints(geometry, blocks_, 0.0);
    juce::Path path;
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (i == 0) {
            path.startNewSubPath(points[i].x, points[i].y);
        } else {
            path.lineTo(points[i].x, points[i].y);
        }
    }
    g.setColour(rta::view::trace);
    g.strokePath(path, juce::PathStrokeType(1.8f));

    // The current-value chip -- the LAST block's own reading, through the
    // same two formatters the headless model itself uses (D3): no third
    // formatter invented at the draw layer either.
    const auto& last = blocks_.back();
    const double lastLevelDb =
        10.0 * std::log10(last.sumSquares / static_cast<double>(last.blockSamples));
    juce::Rectangle<int> chip(stripArea_.getRight() - 220, stripArea_.getY() + az::ui::gap, 220,
                              az::ui::fieldHeight * 2);
    rta::dev::preview::drawChip(g, chip, "LAeq,FAST", rta::view::splCurrentLevelLabel(lastLevelDb),
                                 rta::view::readoutText);
}
