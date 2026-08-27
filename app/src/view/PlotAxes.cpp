// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.5.
#include "view/PlotAxes.h"

#include "view/MeasureColours.h"

#include <az_ui/az_ui.h>

#include <cmath>

namespace rta::view {

namespace {

/// The dB gridlines, top to bottom: one every 10 dB, snapped to a multiple
/// of 10 so a non-round `dbTop` (there isn't one today, but a future
/// headroom setting could introduce one) still lands on tidy numbers
/// instead of drifting off them.
std::vector<double> levelTicks(const PlotGeometry& geometry) {
    std::vector<double> ticks;
    if (geometry.dbBottom >= geometry.dbTop) return ticks;

    const double start = std::floor(geometry.dbTop / 10.0) * 10.0;
    for (double db = start; db >= geometry.dbBottom - 1e-6; db -= 10.0) {
        if (db <= geometry.dbTop + 1e-6) ticks.push_back(db);
    }
    return ticks;
}

/// One hairline groove -- the same dark-line-plus-faint-sheen bevel
/// `az::ui::drawEngravedDivider` uses for a horizontal band, drawn here as
/// a single line because the grid's lines are 1 px, not a full band.
void hairline(juce::Graphics& g, float x1, float y1, float x2, float y2) {
    g.setColour(rta::view::grid);
    g.drawLine(x1, y1, x2, y2, 1.0f);
}

}  // namespace

void drawGrid(juce::Graphics& g, const PlotGeometry& geometry) {
    for (const double hz : decadeTicks(geometry.fLowHz, geometry.fHighHz)) {
        const float x = geometry.xForHz(hz);
        hairline(g, x, geometry.top, x, geometry.bottom);
    }

    for (const double db : levelTicks(geometry)) {
        const float y = geometry.yForDb(db);
        hairline(g, geometry.left, y, geometry.right, y);
    }

    // The plot's own frame: the four hairlines above stop short of drawing
    // the two ends of each axis they do not cross (the top of the lowest
    // frequency tick's line, say), so a 1 px border closes the box.
    g.setColour(rta::view::grid);
    g.drawRect(juce::Rectangle<float>(geometry.left, geometry.top, geometry.right - geometry.left,
                                       geometry.bottom - geometry.top),
               1.0f);
}

void drawFrequencyLabels(juce::Graphics& g, const PlotGeometry& geometry) {
    const auto font = az::ui::monoFont(az::ui::hintFontSize);
    g.setFont(font);
    g.setColour(rta::view::axisText);

    for (const double hz : decadeTicks(geometry.fLowHz, geometry.fHighHz)) {
        const float x = geometry.xForHz(hz);
        // Whole hertz, no decimal, no `k` -- CLAUDE.md "Reading out
        // numbers"; plan trap T-9.
        const juce::String label(static_cast<juce::int64>(std::llround(hz)));

        juce::Rectangle<float> cell(x - kFrequencyLabelHalfWidth, geometry.bottom + 2.0f,
                                     kFrequencyLabelHalfWidth * 2.0f,
                                     static_cast<float>(kFrequencyLabelHeight));
        g.drawText(label, cell, juce::Justification::centred, false);
    }
}

void drawLevelLabels(juce::Graphics& g, const PlotGeometry& geometry) {
    const auto font = az::ui::monoFont(az::ui::hintFontSize);
    g.setFont(font);
    g.setColour(rta::view::axisText);

    constexpr float kHalfHeight = 7.0f;

    for (const double db : levelTicks(geometry)) {
        const float y = geometry.yForDb(db);
        // One decimal, always -- 0.1 dB is a real, actionable difference;
        // plan trap T-9 says explicitly not to unify this with the
        // frequency rule above for tidiness.
        const juce::String label(db, 1);

        juce::Rectangle<float> cell(geometry.left - static_cast<float>(kLevelLabelWidth),
                                     y - kHalfHeight,
                                     static_cast<float>(kLevelLabelWidth) - 6.0f, kHalfHeight * 2.0f);
        g.drawText(label, cell, juce::Justification::centredRight, false);
    }
}

}  // namespace rta::view
