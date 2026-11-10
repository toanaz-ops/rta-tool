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
#include <utility>

namespace rta::view {
namespace {

using detail::drawEmpty;
using detail::kMarginBottom;
using detail::kMarginLeft;
using detail::kMarginRight;
using detail::kMarginTop;

constexpr double kEtcFloorDb = -80.0;
constexpr double kMarkerSnapMs = 0.1;

// A readable time step for a span of `total` ms: 1, 2, 5, 10 ...
double niceStepMs(double total) {
    const double raw = total / 8.0;
    const double decade = std::pow(10.0, std::floor(std::log10(raw)));
    for (const double m : {1.0, 2.0, 5.0, 10.0}) {
        if (m * decade >= raw) return m * decade;
    }
    return 10.0 * decade;
}

}  // namespace

// ---- IrEtcPlot ---------------------------------------------------------------

void IrEtcPlot::setData(std::shared_ptr<const rta::measure::IrEtc> etc, const rta::measure::IrGateMarkers& markers,
                        bool gateOk) {
    etc_ = std::move(etc);
    markers_ = markers;
    gateOk_ = gateOk;
    repaint();
}

juce::Rectangle<float> IrEtcPlot::plotArea() const {
    return getLocalBounds()
        .withTrimmedLeft(kMarginLeft)
        .withTrimmedRight(kMarginRight)
        .withTrimmedTop(kMarginTop)
        .withTrimmedBottom(kMarginBottom)
        .toFloat();
}

float IrEtcPlot::xOfMs(double ms) const {
    const auto plot = plotArea();
    const double span = etc_ ? etc_->endMs() - etc_->startMs : 1.0;
    const double t = span > 0.0 ? (ms - etc_->startMs) / span : 0.0;
    return plot.getX() + static_cast<float>(t) * plot.getWidth();
}

double IrEtcPlot::msOfX(float x) const {
    const auto plot = plotArea();
    const double span = etc_->endMs() - etc_->startMs;
    return etc_->startMs + static_cast<double>((x - plot.getX()) / plot.getWidth()) * span;
}

juce::Point<int> IrEtcPlot::markerCentreForTest(bool start) const {
    const auto plot = plotArea();
    return {static_cast<int>(xOfMs(start ? markers_.startMs : markers_.endMs)), static_cast<int>(plot.getCentreY())};
}

void IrEtcPlot::paint(juce::Graphics& g) {
    drawnPoints_ = 0;
    if (!etc_ || etc_->db.size() < 2 || getWidth() < 120 || getHeight() < 60) {
        drawEmpty(g, getLocalBounds(), "PICK AN IR TO PLACE THE GATE");
        return;
    }
    g.fillAll(az::ui::background);
    const auto plot = plotArea();
    const auto yOf = [&](double db) {
        const double t = std::clamp(db, kEtcFloorDb, 0.0) / kEtcFloorDb;
        return plot.getY() + static_cast<float>(t) * plot.getHeight();
    };

    g.setFont(az::ui::monoFont(az::ui::readoutFontSize));
    for (double db = 0.0; db >= kEtcFloorDb; db -= 20.0) {
        g.setColour(grid);
        g.drawHorizontalLine(static_cast<int>(yOf(db)), plot.getX(), plot.getRight());
        g.setColour(axisText);
        g.drawText(juce::String(static_cast<int>(db)), 0, static_cast<int>(yOf(db)) - 7, kMarginLeft - 6, 14,
                   juce::Justification::centredRight, false);
    }
    const double step = niceStepMs(etc_->endMs() - etc_->startMs);
    for (double t = std::ceil(etc_->startMs / step) * step; t <= etc_->endMs() + 1e-9; t += step) {
        g.setColour(grid);
        g.drawVerticalLine(static_cast<int>(xOfMs(t)), plot.getY(), plot.getBottom());
        g.setColour(axisText);
        char label[24];
        // The tick loop reaches the arrival as a hair below zero; print it as 0, not "-0".
        std::snprintf(label, sizeof label, "%.0f", std::abs(t) < 1.0e-9 ? 0.0 : t);
        g.drawText(label, static_cast<int>(xOfMs(t)) - 20, static_cast<int>(plot.getBottom()) + 2, 40, 14,
                   juce::Justification::centred, false);
    }
    // The gate: shaded between the markers, in the warning colour when the model
    // refused it, so a refused placement is visible where it is made.
    const float xa = std::clamp(xOfMs(markers_.startMs), plot.getX(), plot.getRight());
    const float xb = std::clamp(xOfMs(markers_.endMs), plot.getX(), plot.getRight());
    if (xb > xa) {
        g.setColour((gateOk_ ? az::ui::accent : az::ui::warn).withAlpha(0.12f));
        g.fillRect(xa, plot.getY(), xb - xa, plot.getHeight());
    }

    juce::Path curve;
    for (std::size_t i = 0; i < etc_->db.size(); ++i) {
        const float x = xOfMs(etc_->startMs + etc_->stepMs * static_cast<double>(i));
        const float y = yOf(etc_->db[i]);
        if (i == 0) curve.startNewSubPath(x, y);
        else curve.lineTo(x, y);
    }
    drawnPoints_ = etc_->db.size();
    g.setColour(az::ui::text);
    g.strokePath(curve, juce::PathStrokeType(1.2f));

    for (const bool start : {true, false}) {
        const double ms = start ? markers_.startMs : markers_.endMs;
        const float x = xOfMs(ms);
        if (x < plot.getX() - 1.0f || x > plot.getRight() + 1.0f) continue;
        g.setColour(gateOk_ ? az::ui::accent : az::ui::warn);
        g.drawVerticalLine(static_cast<int>(x), plot.getY(), plot.getBottom());
        // A handle at the top: the thing to grab.
        g.fillRect(x - 4.0f, plot.getY() - 6.0f, 8.0f, 8.0f);
    }
}

void IrEtcPlot::mouseDown(const juce::MouseEvent& event) {
    if (!etc_ || etc_->db.size() < 2) return;
    // The nearer marker follows the hand; both may sit at one place only when the
    // operator put them there, and then the start moves (it can always be pulled left).
    const float x = event.position.x;
    draggingStart_ = std::abs(x - xOfMs(markers_.startMs)) <= std::abs(x - xOfMs(markers_.endMs));
    dragTo(x);
}

void IrEtcPlot::mouseUp(const juce::MouseEvent&) {
    if (onMarkersSettled) onMarkersSettled();
}

void IrEtcPlot::mouseDrag(const juce::MouseEvent& event) {
    if (!etc_ || etc_->db.size() < 2) return;
    dragTo(event.position.x);
}

void IrEtcPlot::dragTo(float x) {
    // Snapped to 0.1 ms and held inside the drawn range: the plot can only be
    // pointed at what it shows. Whether the PAIR is a valid gate is the model's call.
    double ms = std::round(msOfX(x) / kMarkerSnapMs) * kMarkerSnapMs;
    ms = std::clamp(ms, std::ceil(etc_->startMs / kMarkerSnapMs) * kMarkerSnapMs,
                    std::floor(etc_->endMs() / kMarkerSnapMs) * kMarkerSnapMs);
    auto next = markers_;
    (draggingStart_ ? next.startMs : next.endMs) = ms;
    if (next.startMs == markers_.startMs && next.endMs == markers_.endMs) return;
    markers_ = next;
    repaint();
    if (onMarkersChanged) onMarkersChanged(markers_);
}

}  // namespace rta::view
