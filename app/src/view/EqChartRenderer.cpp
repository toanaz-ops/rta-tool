// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See EqChartRenderer.h.
#include "view/EqChartRenderer.h"

#include "view/AxisMetrics.h"
#include "view/MeasureColours.h"
#include "view/PlotAxes.h"
#include "view/PlotGeometry.h"

#include <az_ui/az_ui.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace rta::view {
namespace {

constexpr double kFrequencyLowHz = 20.0;
constexpr double kFrequencyHighHz = 20000.0;

/// Padding past the data, and the narrowest chart worth drawing.
constexpr double kRangePaddingDb = 3.0;
constexpr double kMinimumSpanDb = 30.0;

/// The bin's own slice of a log axis: from the geometric mean with its lower
/// neighbour to the geometric mean with its upper one, so a run of bins shades
/// a contiguous band instead of a picket fence.
double edgeLowHz(std::span<const float> hz, std::size_t k) {
    const double f = static_cast<double>(hz[k]);
    return (k > 0 && hz[k - 1] > 0.0f) ? std::sqrt(static_cast<double>(hz[k - 1]) * f) : f;
}

double edgeHighHz(std::span<const float> hz, std::size_t k) {
    const double f = static_cast<double>(hz[k]);
    return (k + 1 < hz.size()) ? std::sqrt(f * static_cast<double>(hz[k + 1])) : f;
}

float clampedX(const PlotGeometry& geometry, double hz) {
    return std::clamp(geometry.xForHz(std::max(hz, geometry.fLowHz)), geometry.left, geometry.right);
}

/// `mask` non-empty: the path BREAKS at every bin whose mask is 0, so a curve
/// is drawn only where it is judged (see paintEqChart). Empty: every bin.
template <typename Values>
juce::Path curvePath(const PlotGeometry& geometry, std::span<const float> hz, const Values& values,
                     std::span<const std::uint8_t> mask = {}) {
    juce::Path path;
    bool started = false;
    for (std::size_t k = 0; k < hz.size() && k < values.size(); ++k) {
        const double f = static_cast<double>(hz[k]);
        const double v = static_cast<double>(values[k]);
        if (!mask.empty() && (k >= mask.size() || mask[k] == 0)) {
            started = false;
            continue;
        }
        if (f < geometry.fLowHz || f > geometry.fHighHz || !std::isfinite(v)) continue;
        const float x = geometry.xForHz(f);
        const float y = geometry.yForDb(v);
        if (started) path.lineTo(x, y);
        else path.startNewSubPath(x, y);
        started = true;
    }
    return path;
}

/// Calls `paintRun(x0, x1)` for every maximal run of bins whose mask equals
/// `wanted` and whose frequency is on the axis.
template <typename PaintRun>
void forEachMaskRun(const PlotGeometry& geometry, std::span<const float> hz,
                    std::span<const std::uint8_t> mask, bool wanted, PaintRun paintRun) {
    std::size_t k = 0;
    while (k < hz.size() && k < mask.size()) {
        if (hz[k] <= 0.0f || (mask[k] != 0) != wanted) { ++k; continue; }
        const std::size_t first = k;
        while (k + 1 < hz.size() && k + 1 < mask.size() && hz[k + 1] > 0.0f && (mask[k + 1] != 0) == wanted) ++k;
        paintRun(clampedX(geometry, edgeLowHz(hz, first)), clampedX(geometry, edgeHighHz(hz, k)));
        ++k;
    }
}

void hatch(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour colour) {
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(box.toNearestInt());
    g.setColour(colour);
    constexpr float kStep = 6.0f;
    for (float x = box.getX() - box.getHeight(); x < box.getRight(); x += kStep) {
        g.drawLine(x, box.getBottom(), x + box.getHeight(), box.getY(), 1.0f);
    }
}

void paintMasks(juce::Graphics& g, const PlotGeometry& geometry, const EqChartData& data) {
    const float top = geometry.top;
    const float height = geometry.bottom - geometry.top;
    // Untrusted: judged by nobody -- dimmed fill, the same "refuse to judge"
    // tone the RTA view and the tuning grammar already use.
    forEachMaskRun(geometry, data.hz, data.trusted, false, [&](float x0, float x1) {
        g.setColour(untrusted.withAlpha(0.22f));
        g.fillRect(x0, top, std::max(x1 - x0, 1.0f), height);
    });
    // Declined: the operator refused this band -- hatched, so it cannot be
    // mistaken for the (filled) untrusted shading.
    forEachMaskRun(geometry, data.hz, data.excluded, true, [&](float x0, float x1) {
        hatch(g, { x0, top, std::max(x1 - x0, 2.0f), height }, borderline.withAlpha(0.55f));
    });
}

void strokeCurve(juce::Graphics& g, const juce::Path& path, juce::Colour colour, float thickness,
                 bool dashed) {
    g.setColour(colour);
    if (!dashed) {
        g.strokePath(path, juce::PathStrokeType(thickness));
        return;
    }
    constexpr float kDashes[] = { 5.0f, 4.0f };
    juce::Path dashedPath;
    juce::PathStrokeType(thickness).createDashedStroke(dashedPath, path, kDashes, 2);
    g.strokePath(dashedPath, juce::PathStrokeType(thickness));
}

void paintLegend(juce::Graphics& g, const PlotGeometry& geometry) {
    g.setFont(az::ui::monoFont(az::ui::tableFontSize));
    float x = geometry.left + 8.0f;
    const float y = geometry.top + 4.0f;
    const struct { const char* text; juce::Colour colour; } items[] = {
        { "MEASURED", trace }, { "GHOST", ghost }, { "TARGET + c", target } };
    for (const auto& item : items) {
        const float width = az::ui::stringWidth(az::ui::monoFont(az::ui::tableFontSize), item.text) + 14.0f;
        g.setColour(item.colour);
        g.drawText(item.text, juce::Rectangle<float>(x, y, width, 14.0f), juce::Justification::centredLeft, false);
        x += width;
    }
}

}  // namespace

EqDbRange eqChartDbRange(const EqChartData& data) {
    // The range follows the TRUSTED bins: below the coherence floor a transfer
    // estimate is a ratio of noise (a SYNTHETIC capture reads +130 dB where the
    // reference has no energy), and letting those bins set the axis would
    // squash the part of the curve the allocator actually fits into a line.
    // Untrusted bins are still drawn -- clamped at the frame -- and shaded.
    // With no trusted bin at all, every bin counts (nothing better to show).
    const auto scan = [&](bool trustedOnly, double& lo, double& hi) {
        const auto take = [&](double v) {
            if (!std::isfinite(v)) return;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        };
        for (std::size_t k = 0; k < data.hz.size(); ++k) {
            if (data.hz[k] < kFrequencyLowHz) continue;
            if (trustedOnly && (k >= data.trusted.size() || data.trusted[k] == 0)) continue;
            if (k < data.measuredDb.size()) take(static_cast<double>(data.measuredDb[k]));
            if (k < data.ghostDb.size()) take(data.ghostDb[k]);
            if (k < data.targetLineDb.size()) take(data.targetLineDb[k]);
        }
    };
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    scan(true, lo, hi);
    if (!(lo <= hi)) scan(false, lo, hi);
    if (!(lo <= hi)) return {};
    double top = std::ceil((hi + kRangePaddingDb) / 10.0) * 10.0;
    double bottom = std::floor((lo - kRangePaddingDb) / 10.0) * 10.0;
    while (top - bottom < kMinimumSpanDb) {  // grow alternately so the data stays centred
        top += 10.0;
        if (top - bottom < kMinimumSpanDb) bottom -= 10.0;
    }
    return { top, bottom };
}

double eqStripHalfRangeDb(std::span<const double> correctionDb) {
    double peak = 0.0;
    for (const double v : correctionDb) {
        if (std::isfinite(v)) peak = std::max(peak, std::abs(v));
    }
    return std::max(10.0, std::ceil(peak / 10.0) * 10.0);
}

void paintEqChart(juce::Graphics& g, juce::Rectangle<int> area, const EqChartData& data) {
    auto plot = area;
    plot.removeFromLeft(kLevelLabelWidth);
    plot.removeFromRight(static_cast<int>(kFrequencyLabelHalfWidth));
    plot.removeFromBottom(kFrequencyLabelHeight + 2);
    if (plot.getHeight() < 60 || plot.getWidth() < 60) return;

    constexpr int kStripFraction = 30;  // percent of the plot height
    const int stripHeight = plot.getHeight() * kStripFraction / 100;
    const auto strip = plot.removeFromBottom(stripHeight);
    plot.removeFromBottom(az::ui::gap);

    const double fHigh = std::min(kFrequencyHighHz, data.sampleRate / 2.0);
    const auto range = eqChartDbRange(data);
    PlotGeometry upper;
    upper.left = static_cast<float>(plot.getX());
    upper.right = static_cast<float>(plot.getRight());
    upper.top = static_cast<float>(plot.getY());
    upper.bottom = static_cast<float>(plot.getBottom());
    upper.fLowHz = kFrequencyLowHz;
    upper.fHighHz = fHigh;
    upper.dbTop = range.top;
    upper.dbBottom = range.bottom;

    PlotGeometry lower = upper;
    lower.top = static_cast<float>(strip.getY());
    lower.bottom = static_cast<float>(strip.getBottom());
    const double half = eqStripHalfRangeDb(data.correctionDb);
    lower.dbTop = half;
    lower.dbBottom = -half;

    drawGrid(g, upper);
    drawLevelLabels(g, upper);
    paintMasks(g, upper, data);
    strokeCurve(g, curvePath(upper, data.hz, data.targetLineDb), target, 1.0f, false);
    // Measured and ghost are drawn only where the bin is trusted: below the
    // coherence floor the estimate is a ratio of noise (ten of dB of scatter,
    // clamped against the frame) and drawing it would bury the curve the
    // allocator fits. The shading above marks the region as not judged. With
    // NO trusted bin the mask is dropped, so the chart is never blank.
    const bool anyTrusted = std::any_of(data.trusted.begin(), data.trusted.end(),
                                        [](std::uint8_t t) { return t != 0; });
    const auto judged = anyTrusted ? data.trusted : std::span<const std::uint8_t>{};
    strokeCurve(g, curvePath(upper, data.hz, data.measuredDb, judged), trace, 1.5f, false);
    strokeCurve(g, curvePath(upper, data.hz, data.ghostDb, judged), ghost, 1.5f, true);
    paintLegend(g, upper);

    drawGrid(g, lower);
    drawLevelLabels(g, lower);
    paintMasks(g, lower, data);
    // The zero line, so "no correction" reads as a level, not as an absence.
    g.setColour(axisText);
    g.drawLine(lower.left, lower.yForDb(0.0), lower.right, lower.yForDb(0.0), 1.0f);
    strokeCurve(g, curvePath(lower, data.hz, data.correctionDb), secondaryTrace, 1.5f, false);
    drawFrequencyLabels(g, lower);
}

}  // namespace rta::view
