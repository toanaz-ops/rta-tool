// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See Rt60Chart.h.
#include "view/Rt60Chart.h"

#include "view/MeasureColours.h"

#include <az_ui/az_ui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rta::view {
namespace {

constexpr int kMarginLeft = 44;
constexpr int kMarginRight = 10;
constexpr int kMarginTop = 18;
constexpr int kMarginBottom = 18;
constexpr float kFloorStepDb = 10.0f;
constexpr float kShallowestFloorDb = -40.0f;  // never zoom in tighter than a T30 span plus margin

struct SpanStyle {
    const char* name;
    const rta::measure::Rt60FitSpan* span;
    juce::Colour colour;
    float thickness;
};

std::string legendLine(const char* name, const rta::measure::Rt60FitSpan& span) {
    char text[96];
    std::snprintf(text, sizeof text, "%s %.2f s (%.0f to %.0f dB)", name, -60.0 / span.slopeDbPerSec, span.upperDb,
                  span.lowerDb);
    return text;
}

// A readable time-axis step for a span of `total` seconds: 0.1, 0.2, 0.5, 1, 2 ...
double niceStep(double total) {
    const double raw = total / 6.0;
    const double decade = std::pow(10.0, std::floor(std::log10(raw)));
    for (const double m : {1.0, 2.0, 5.0, 10.0}) {
        if (m * decade >= raw) return m * decade;
    }
    return 10.0 * decade;
}

}  // namespace

void Rt60Chart::setCurve(const rta::measure::Rt60Edc& edc, const std::string& caption) {
    edc_ = edc;
    caption_ = caption;
    repaint();
}

void Rt60Chart::clear() {
    edc_ = {};
    caption_.clear();
    repaint();
}

void Rt60Chart::paint(juce::Graphics& g) {
    g.fillAll(az::ui::background);
    drawn_.clear();
    legend_.clear();

    const auto bounds = getLocalBounds();
    if (!edc_.valid() || edc_.db.size() < 2 || bounds.getWidth() < 120 || bounds.getHeight() < 60) {
        g.setColour(emptyStateText);
        g.setFont(az::ui::legendFont(az::ui::captionFontSize, true, az::ui::trackingCaption));
        g.drawFittedText(caption_.empty() ? juce::String("CLICK A BAND ROW FOR ITS DECAY CURVE")
                                          : juce::String(juce::CharPointer_UTF8(caption_.c_str())),
                         bounds.reduced(az::ui::gap * 2),
                         juce::Justification::centred, 2);
        return;
    }

    const auto plot = bounds.withTrimmedLeft(kMarginLeft)
                          .withTrimmedRight(kMarginRight)
                          .withTrimmedTop(kMarginTop)
                          .withTrimmedBottom(kMarginBottom)
                          .toFloat();
    const double totalSec = static_cast<double>(edc_.db.size() - 1) * edc_.stepSeconds;
    const float last = edc_.db.back();
    const float floorDb = std::min(kShallowestFloorDb, std::floor(last / kFloorStepDb) * kFloorStepDb);
    const auto xOf = [&](double sec) {
        return plot.getX() + static_cast<float>(sec / totalSec) * plot.getWidth();
    };
    const auto yOf = [&](double db) {
        return plot.getY() + static_cast<float>(db / floorDb) * plot.getHeight();
    };

    // Grid: a hairline every 10 dB, labelled at the left, and a time tick at a
    // readable step along the bottom.
    g.setFont(az::ui::monoFont(az::ui::readoutFontSize));
    // Labels thin out (every 20, 40 dB) when the plot is too short to keep them apart.
    const float pixelsPerStep = plot.getHeight() * kFloorStepDb / -floorDb;
    const int labelEvery = pixelsPerStep >= 14.0f ? 1 : (pixelsPerStep >= 7.0f ? 2 : 4);
    int stepIndex = 0;
    for (float db = 0.0f; db >= floorDb; db -= kFloorStepDb, ++stepIndex) {
        g.setColour(grid);
        g.drawHorizontalLine(static_cast<int>(yOf(db)), plot.getX(), plot.getRight());
        if (stepIndex % labelEvery != 0) continue;
        g.setColour(axisText);
        g.drawText(juce::String(static_cast<int>(db)), bounds.getX(), static_cast<int>(yOf(db)) - 7, kMarginLeft - 6, 14,
                   juce::Justification::centredRight, false);
    }
    const double step = niceStep(totalSec);
    for (double t = 0.0; t <= totalSec + 1e-9; t += step) {
        g.setColour(grid);
        g.drawVerticalLine(static_cast<int>(xOf(t)), plot.getY(), plot.getBottom());
        g.setColour(axisText);
        char label[24];
        std::snprintf(label, sizeof label, step < 1.0 ? "%.1f s" : "%.0f s", t);
        g.drawText(label, static_cast<int>(xOf(t)) - 24, static_cast<int>(plot.getBottom()) + 2, 48, 14,
                   juce::Justification::centred, false);
    }

    // The curve: every point, in order. This is the data the test reads back.
    juce::Path curve;
    for (std::size_t i = 0; i < edc_.db.size(); ++i) {
        const float x = xOf(static_cast<double>(i) * edc_.stepSeconds);
        const float y = yOf(std::max(edc_.db[i], floorDb));
        if (i == 0) curve.startNewSubPath(x, y);
        else curve.lineTo(x, y);
        drawn_.push_back(edc_.db[i]);
    }
    g.setColour(az::ui::text);
    g.strokePath(curve, juce::PathStrokeType(1.5f));

    // The three fits, each as the least-squares line core fitted over the span it
    // fitted, with hairlines at the span's ends. EDT is drawn in the warning
    // colour on purpose: it carries no validity check (record section 9).
    const SpanStyle styles[] = {
        {"EDT", &edc_.edt, az::ui::warn, 1.5f},
        {"T20", &edc_.t20, az::ui::dim, 2.0f},
        {"T30", &edc_.t30, az::ui::accent, 2.5f},
    };
    for (const auto& style : styles) {
        const auto& span = *style.span;
        if (!span.valid) continue;
        const auto lineAt = [&span](double sec) { return span.interceptDb + span.slopeDbPerSec * sec; };
        g.setColour(style.colour.withAlpha(0.35f));
        g.drawVerticalLine(static_cast<int>(xOf(span.startSec)), plot.getY(), plot.getBottom());
        g.drawVerticalLine(static_cast<int>(xOf(span.endSec)), plot.getY(), plot.getBottom());
        g.setColour(style.colour);
        g.drawLine(xOf(span.startSec), yOf(std::max(lineAt(span.startSec), static_cast<double>(floorDb))),
                   xOf(span.endSec), yOf(std::max(lineAt(span.endSec), static_cast<double>(floorDb))), style.thickness);
        legend_.push_back(legendLine(style.name, span));
    }

    // Caption top-left, legend top-right.
    g.setFont(az::ui::monoFont(az::ui::readoutFontSize));
    g.setColour(az::ui::dim);
    g.drawText(juce::String(juce::CharPointer_UTF8(caption_.c_str())), bounds.getX() + kMarginLeft, 0,
               bounds.getWidth() * 2 / 5, kMarginTop, juce::Justification::centredLeft, true);
    juce::String legendText;
    for (const auto& line : legend_) legendText += juce::String(line) + "   ";
    g.setColour(az::ui::text);
    g.drawText(legendText.trimEnd(), bounds.getX() + bounds.getWidth() * 2 / 5, 0, bounds.getWidth() * 3 / 5 - kMarginRight,
               kMarginTop, juce::Justification::centredRight, true);
}

}  // namespace rta::view
