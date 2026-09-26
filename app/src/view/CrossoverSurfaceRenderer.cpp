// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See CrossoverSurfaceRenderer.h.
#include "view/CrossoverSurfaceRenderer.h"

#include "view/AxisMetrics.h"
#include "view/MeasureColours.h"
#include "view/PlotAxes.h"

#include <az_ui/az_ui.h>

#include <array>
#include <cmath>

namespace rta::view {
namespace {

constexpr double kPi = 3.14159265358979323846;

/// Room for the top decade's frequency label, the same margin RtaView.cpp
/// reserves for the same reason.
constexpr int kRightMargin = static_cast<int>(kFrequencyLabelHalfWidth);

PlotGeometry makeGeometry(juce::Rectangle<int> area, double top, double bottom) {
    PlotGeometry geometry;
    geometry.left = static_cast<float>(area.getX() + kLevelLabelWidth);
    geometry.right = static_cast<float>(area.getRight() - kRightMargin);
    geometry.top = static_cast<float>(area.getY());
    geometry.bottom = static_cast<float>(area.getBottom());
    geometry.fLowHz = 20.0;
    geometry.fHighHz = 20000.0;
    geometry.dbTop = top;
    geometry.dbBottom = bottom;
    return geometry;
}

/// Whole degrees, not the dB view's forced one decimal: different
/// quantities, different rules (CLAUDE.md "Reading out numbers").
void drawDegreeLabels(juce::Graphics& g, const PlotGeometry& geometry) {
    g.setFont(az::ui::monoFont(az::ui::tableFontSize));
    g.setColour(axisText);
    for (double deg = geometry.dbTop; deg >= geometry.dbBottom - 1e-6; deg -= 90.0) {
        const float y = geometry.yForDb(deg);
        juce::Rectangle<float> cell(geometry.left - static_cast<float>(kLevelLabelWidth), y - 7.0f,
                                    static_cast<float>(kLevelLabelWidth) - 6.0f, 14.0f);
        g.drawText(juce::String(static_cast<int>(std::llround(deg))), cell,
                   juce::Justification::centredRight, false);
    }
}

/// Strokes a dB series over the log-frequency axis, one bin per point.
void strokeSeries(juce::Graphics& g, const PlotGeometry& geometry, const std::vector<float>& db,
                  double binWidthHz, juce::Colour colour, float thickness, bool dashed = false) {
    juce::Path path;
    bool started = false;
    for (std::size_t k = 1; k < db.size(); ++k) {
        const double hz = static_cast<double>(k) * binWidthHz;
        if (hz < geometry.fLowHz || hz > geometry.fHighHz) continue;
        const float x = geometry.xForHz(hz);
        const float y = geometry.yForDb(db[k]);
        if (!started) {
            path.startNewSubPath(x, y);
            started = true;
        } else {
            path.lineTo(x, y);
        }
    }
    g.setColour(colour);
    if (!dashed) {
        g.strokePath(path, juce::PathStrokeType(thickness));
        return;
    }
    juce::Path stroked;
    const std::array<float, 2> dashLengths{ 6.0f, 4.0f };
    juce::PathStrokeType(thickness).createDashedStroke(stroked, path, dashLengths.data(), 2);
    g.fillPath(stroked);
}

}  // namespace

PlotGeometry crossoverPhaseGeometry(juce::Rectangle<int> area) { return makeGeometry(area, 180.0, -180.0); }

PlotGeometry crossoverSummationGeometry(juce::Rectangle<int> area) {
    return makeGeometry(area, 12.0, -24.0);
}

void paintCrossoverPhase(juce::Graphics& g, juce::Rectangle<int> area,
                         const CrossoverSurface& surface) {
    const PlotGeometry geometry = crossoverPhaseGeometry(area);

    // The fit window, washed under the grid: this is the stretch of spectrum
    // the relative-phase trace is defined on, and the only one the verdict
    // reads (record Sec.4).
    const auto& relative = surface.relativePhase();
    if (!relative.empty()) {
        const float left = geometry.xForHz(relative.front().hz);
        const float right = geometry.xForHz(relative.back().hz);
        g.setColour(az::ui::accent.withAlpha(0.07f));
        g.fillRect(juce::Rectangle<float>(left, geometry.top, right - left,
                                          geometry.bottom - geometry.top));
    }

    drawGrid(g, geometry);
    drawFrequencyLabels(g, geometry);
    drawDegreeLabels(g, geometry);

    // THE ASKED LINE. Horizontal, at the offset record Sec.3 predicts for the
    // topology the operator (or, on the live pane, this task's fixed
    // default) NAMED -- nothing on this plot derived it.
    const double targetDeg = surface.targetRadians() * 180.0 / kPi;
    const float targetY = geometry.yForDb(targetDeg);
    g.setColour(target);
    for (float x = geometry.left; x < geometry.right; x += 6.0f) {
        g.fillRect(x, targetY - 0.5f, 3.0f, 1.0f);
    }

    juce::Path path;
    bool started = false;
    for (const auto& point : relative) {
        const float x = geometry.xForHz(point.hz);
        const float y = geometry.yForDb(point.radians * 180.0 / kPi);
        if (!started) {
            path.startNewSubPath(x, y);
            started = true;
        } else {
            path.lineTo(x, y);
        }
    }
    g.setColour(match);
    g.strokePath(path, juce::PathStrokeType(2.0f));
}

void paintCrossoverSummation(juce::Graphics& g, juce::Rectangle<int> area,
                             const CrossoverSurface& surface) {
    const PlotGeometry geometry = crossoverSummationGeometry(area);
    const double binWidthHz = surface.binWidthHz();

    drawGrid(g, geometry);
    drawFrequencyLabels(g, geometry);
    drawLevelLabels(g, geometry);

    // MARKS, not criteria: +6.02 dB for two coherent sources, and what this
    // topology is designed to sum to at fc.
    for (const double markDb : { surface.marks().coherentSumDb, surface.marks().designedSumDb }) {
        const float y = geometry.yForDb(markDb);
        g.setColour(target.withAlpha(0.55f));
        g.fillRect(geometry.left, y - 0.5f, geometry.right - geometry.left, 1.0f);
        // Labelled at the RIGHT edge: the left is where a caller's own
        // legend/chip sits, and a mark label under one would read as part
        // of it.
        g.setFont(az::ui::monoFont(az::ui::tableFontSize));
        g.drawText(juce::String(markDb, 1) + " dB",
                   juce::Rectangle<float>(geometry.right - 92.0f, y - 14.0f, 88.0f, 12.0f),
                   juce::Justification::centredRight, false);
    }

    strokeSeries(g, geometry, surface.highSideDb(), binWidthHz, trace, 1.4f);
    strokeSeries(g, geometry, surface.lowSideDb(), binWidthHz, secondaryTrace, 1.4f);
    strokeSeries(g, geometry, surface.ghostSumDb(), binWidthHz, ghost, 1.8f, true);
    strokeSeries(g, geometry, surface.predictedSumDb(), binWidthHz, match, 2.2f);

    g.setColour(az::ui::faded);
    g.setFont(az::ui::legendFont(az::ui::columnFontSize, true, az::ui::trackingColumn));
    g.drawText("PREDICTED SUM (SOLID)   PRE-ALIGNMENT GHOST (DASHED)",
               juce::Rectangle<int>(static_cast<int>(geometry.left) + az::ui::gap,
                                     static_cast<int>(geometry.top) + az::ui::spacing, 420,
                                     az::ui::captionHeight),
               juce::Justification::centredLeft, false);
}

}  // namespace rta::view
