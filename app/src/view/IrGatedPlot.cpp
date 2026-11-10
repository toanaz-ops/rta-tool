// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See IrPlots.h.
#include "view/IrPlots.h"

#include "view/IrPlotDrawing.h"
#include "view/MeasureColours.h"
#include "view/PlotAxes.h"
#include "view/PlotGeometry.h"

#include <az_ui/az_ui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace rta::view {
namespace {

using detail::drawEmpty;
using detail::kMarginBottom;
using detail::kMarginLeft;
using detail::kMarginRight;
using detail::kMarginTop;

juce::String fromUtf8(const std::string& text) { return juce::String(juce::CharPointer_UTF8(text.c_str())); }

using rta::measure::IrLowerPlot;

constexpr double kExcessAxisDeg = 180.0;

PlotGeometry gatedGeometry(juce::Rectangle<int> bounds) {
    PlotGeometry geometry;
    const auto plot = bounds.withTrimmedLeft(kMarginLeft).withTrimmedRight(kMarginRight)
                          .withTrimmedTop(kMarginTop).withTrimmedBottom(kMarginBottom).toFloat();
    geometry.left = plot.getX();
    geometry.right = plot.getRight();
    geometry.top = plot.getY();
    geometry.bottom = plot.getBottom();
    return geometry;
}

// Frequency grid, then a rule and a left label every `stepDeg` (the y limits read as degrees).
void drawDegreeGrid(juce::Graphics& g, const PlotGeometry& geometry, double stepDeg) {
    g.setFont(az::ui::monoFont(az::ui::readoutFontSize));
    for (const double hz : decadeTicks(geometry.fLowHz, geometry.fHighHz)) {
        g.setColour(grid);
        g.drawVerticalLine(static_cast<int>(geometry.xForHz(hz)), geometry.top, geometry.bottom);
    }
    drawFrequencyLabels(g, geometry);
    for (double deg = std::ceil(geometry.dbBottom / stepDeg) * stepDeg; deg <= geometry.dbTop + 1e-9; deg += stepDeg) {
        const int y = static_cast<int>(geometry.yForDb(deg));
        g.setColour(grid);
        g.drawHorizontalLine(y, geometry.left, geometry.right);
        g.setColour(axisText);
        g.drawText(juce::String(static_cast<int>(std::lround(deg))), 0, y - 7, kMarginLeft - 6, 14,
                   juce::Justification::centredRight, false);
    }
}

// A polyline through (hz, value) points. A step of more than 180 deg between neighbours is
// a wrap of the excess phase, not a line to draw, so the path is broken there.
void strokeSeries(juce::Graphics& g, const PlotGeometry& geometry, const std::vector<std::pair<double, float>>& points,
                  juce::Colour colour, bool breakOnWrap) {
    juce::Path path;
    bool open = false;
    float previous = 0.0f;
    for (const auto& [hz, value] : points) {
        const float x = geometry.xForHz(hz);
        const float y = geometry.yForDb(value);
        if (!open || (breakOnWrap && std::abs(value - previous) > 180.0f)) path.startNewSubPath(x, y);
        else path.lineTo(x, y);
        open = true;
        previous = value;
    }
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(1.5f));
}

// A rounded step (45 deg doubling) so the degree axis carries at most about six rules.
double degreeStepFor(double span) {
    double step = 45.0;
    while (span / step > 6.0) step *= 2.0;
    return step;
}

}  // namespace

void IrGatedPlot::setData(std::shared_ptr<const rta::measure::IrGateAnalysis> analysis,
                          std::shared_ptr<const rta::measure::IrExcessPhase> excess, rta::measure::IrLowerPlot mode,
                          std::string emptyText) {
    analysis_ = std::move(analysis);
    excess_ = std::move(excess);
    mode_ = mode;
    emptyText_ = std::move(emptyText);
    repaint();
}

void IrGatedPlot::paint(juce::Graphics& g) {
    for (auto* v : {&drawn_, &drawnMeasured_, &drawnMinimum_, &drawnExcess_, &drawnGroupDelay_}) v->clear();
    if (!analysis_ || !analysis_->ok || analysis_->bins.size() < 2 || getWidth() < 120 || getHeight() < 60 ||
        (mode_ != IrLowerPlot::Magnitude && (!excess_ || !excess_->valid))) {
        drawEmpty(g, getLocalBounds(), fromUtf8(emptyText_));
        return;
    }
    g.fillAll(az::ui::background);
    auto geometry = gatedGeometry(getLocalBounds());
    if (mode_ == IrLowerPlot::Magnitude) paintMagnitude(g, geometry);
    else if (mode_ == IrLowerPlot::Phase) paintPhase(g, geometry);
    else paintExcess(g, geometry);
}

void IrGatedPlot::paintMagnitude(juce::Graphics& g, PlotGeometry& geometry) {
    // Centred on the response: a 60 dB window around the median of the trusted
    // part would hide a notch, so the top is the loudest bin rounded up to 10 dB.
    const double binHz = analysis_->sampleRate / static_cast<double>(analysis_->transformSize);
    float peakDb = -200.0f;
    std::vector<float> db(analysis_->bins.size());
    for (std::size_t k = 0; k < db.size(); ++k) {
        db[k] = 20.0f * std::log10(std::max(std::abs(analysis_->bins[k]), 1.0e-10f));
        if (static_cast<double>(k) * binHz >= geometry.fLowHz && static_cast<double>(k) * binHz <= geometry.fHighHz) {
            peakDb = std::max(peakDb, db[k]);
        }
    }
    geometry.dbTop = std::ceil(peakDb / 10.0f) * 10.0f + 10.0;
    geometry.dbBottom = geometry.dbTop - 60.0;

    drawGrid(g, geometry);
    g.setFont(az::ui::monoFont(az::ui::readoutFontSize));
    drawFrequencyLabels(g, geometry);
    drawLevelLabels(g, geometry);

    // Below the lowest valid frequency the gate is shorter than the wave: say so
    // by drawing that part dimmer and marking the edge, not by hiding it.
    const float xMin = geometry.xForHz(std::clamp(analysis_->fMinHz, geometry.fLowHz, geometry.fHighHz));
    g.setColour(az::ui::warn.withAlpha(0.10f));
    g.fillRect(geometry.left, geometry.top, xMin - geometry.left, geometry.bottom - geometry.top);
    g.setColour(az::ui::warn);
    g.drawVerticalLine(static_cast<int>(xMin), geometry.top, geometry.bottom);

    std::vector<std::pair<double, float>> points;
    for (std::size_t k = 1; k < db.size(); ++k) {
        const double hz = static_cast<double>(k) * binHz;
        if (hz < geometry.fLowHz || hz > geometry.fHighHz) continue;
        points.emplace_back(hz, db[k]);
        drawn_.push_back(db[k]);
    }
    strokeSeries(g, geometry, points, trace, false);
}

// PHASE: the measured phase and the minimum phase of the same magnitude, unwrapped, on one
// degree axis. Where they part is the excess phase; EXCESS isolates it.
void IrGatedPlot::paintPhase(juce::Graphics& g, PlotGeometry& geometry) {
    const auto& e = *excess_;
    double lo = 1.0e30, hi = -1.0e30;
    for (std::size_t k = 1; k < e.measuredDeg.size(); ++k) {
        const double hz = static_cast<double>(k) * e.binHz;
        if (hz < geometry.fLowHz || hz > geometry.fHighHz) continue;
        for (const float v : {e.measuredDeg[k], e.minimumDeg[k]}) {
            lo = std::min<double>(lo, v);
            hi = std::max<double>(hi, v);
        }
    }
    const double step = degreeStepFor(std::max(hi - lo, 1.0));
    geometry.dbBottom = std::floor(lo / step) * step;
    geometry.dbTop = std::max(std::ceil(hi / step) * step, geometry.dbBottom + step);
    drawDegreeGrid(g, geometry, step);

    std::vector<std::pair<double, float>> measured, minimum;
    for (std::size_t k = 1; k < e.measuredDeg.size(); ++k) {
        const double hz = static_cast<double>(k) * e.binHz;
        if (hz < geometry.fLowHz || hz > geometry.fHighHz) continue;
        measured.emplace_back(hz, e.measuredDeg[k]);
        minimum.emplace_back(hz, e.minimumDeg[k]);
        drawnMeasured_.push_back(e.measuredDeg[k]);
        drawnMinimum_.push_back(e.minimumDeg[k]);
    }
    strokeSeries(g, geometry, measured, trace, false);
    strokeSeries(g, geometry, minimum, secondaryTrace, false);
}

// EXCESS: phi_x on a fixed -180..180 axis (left) and the excess group delay on its own
// symmetric ms axis (right), over the TRUSTED band only -- outside it the kernel held a
// flat magnitude, so a curve there would be the fill, not a measurement.
void IrGatedPlot::paintExcess(juce::Graphics& g, PlotGeometry& geometry) {
    const auto& e = *excess_;
    geometry.dbTop = kExcessAxisDeg;
    geometry.dbBottom = -kExcessAxisDeg;
    drawDegreeGrid(g, geometry, 90.0);

    const double lowHz = std::max(geometry.fLowHz, e.trustedLowHz);
    const double highHz = std::min(geometry.fHighHz, e.trustedHighHz);
    // The ms axis follows the 98th percentile of |tau_x|, not the peak: group delay spikes to
    // tens of milliseconds at a notch, and one such bin would flatten the rest to a line.
    // A spike is clipped at the axis edge instead.
    std::vector<double> gdAbs;
    for (std::size_t k = 1; k < e.excessDeg.size(); ++k) {
        const double hz = static_cast<double>(k) * e.binHz;
        if (hz >= lowHz && hz <= highHz) gdAbs.push_back(std::abs(e.excessGroupDelayMs[k]));
    }
    double gdPeak = 0.0;
    if (!gdAbs.empty()) {
        const auto nth = gdAbs.begin() + static_cast<std::ptrdiff_t>((gdAbs.size() - 1) * 98 / 100);
        std::nth_element(gdAbs.begin(), nth, gdAbs.end());
        gdPeak = *nth;
    }
    const double decade = std::pow(10.0, std::floor(std::log10(std::max(gdPeak, 0.1))));
    double gdAxis = 10.0 * decade;
    for (const double m : {1.0, 2.0, 5.0}) {
        if (m * decade >= gdPeak) {
            gdAxis = m * decade;
            break;
        }
    }
    auto delayGeometry = geometry;
    delayGeometry.dbTop = gdAxis;
    delayGeometry.dbBottom = -gdAxis;
    // The ms axis lives INSIDE the plot's right edge: the right margin is only 10 px wide.
    char label[32];
    g.setColour(az::ui::accent);
    for (const double ms : {gdAxis, 0.0, -gdAxis}) {
        std::snprintf(label, sizeof label, std::abs(ms) < 10.0 ? "%+.1f ms" : "%+.0f ms", ms);
        g.drawText(label, static_cast<int>(geometry.right) - 64, static_cast<int>(delayGeometry.yForDb(ms)) - 7, 62, 14,
                   juce::Justification::centredRight, false);
    }
    g.setColour(axisText);
    g.drawText("excess phase, deg (left) / group delay, ms (right), tau0 removed", static_cast<int>(geometry.left) + 6,
               static_cast<int>(geometry.top) + 2, 420, 14, juce::Justification::centredLeft, false);

    std::vector<std::pair<double, float>> phase, delay;
    for (std::size_t k = 1; k < e.excessDeg.size(); ++k) {
        const double hz = static_cast<double>(k) * e.binHz;
        if (hz < lowHz || hz > highHz) continue;
        phase.emplace_back(hz, e.excessDeg[k]);
        delay.emplace_back(hz, e.excessGroupDelayMs[k]);
        drawnExcess_.push_back(e.excessDeg[k]);
        drawnGroupDelay_.push_back(e.excessGroupDelayMs[k]);
    }
    strokeSeries(g, geometry, phase, trace, true);
    strokeSeries(g, delayGeometry, delay, az::ui::accent.withAlpha(0.55f), false);
}

}  // namespace rta::view
