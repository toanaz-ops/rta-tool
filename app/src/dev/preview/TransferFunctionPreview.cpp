// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/dev/preview. See
// docs/specs/2026-08-28-interactive-tuning-visuals.md.
#include "dev/preview/TransferFunctionPreview.h"

#include "dev/preview/PreviewFurniture.h"
#include "dev/preview/PreviewMath.h"
#include "view/MeasureColours.h"
#include "view/PlotAxes.h"
#include "view/PlotGeometry.h"

#include <az_ui/az_ui.h>

#include <algorithm>
#include <cmath>

namespace {

using rta::view::PlotGeometry;

// ---------------------------------------------------------------------
// Illustrative preview curves. Every number below describes a shape, not a
// measurement -- built from rta::dev::preview's smoothstep / log-gaussian /
// arctan-swing pieces (PreviewMath.h) rather than noise, so the curve is
// something a reader can check against the comment next to it. A real one
// arrives once P2 (dual-FFT) and G16 (environment input) exist to feed a
// device instead of a constant.
// ---------------------------------------------------------------------

// LF rise: a gentle first-order-shelf-shaped lift below 250 Hz, the kind an
// unvented small box or a room's own gain gives it before anyone touches an
// EQ.
constexpr double kLfShelfLowHz = 40.0;
constexpr double kLfShelfHighHz = 250.0;
constexpr double kLfShelfGainDb = 5.5;

// The 2 kHz notch every one of these three previews' curves shares (V1's
// suggestion chips target the SAME frequency in TargetMatchPreview.cpp, on
// purpose -- three views of one loudspeaker's problem, not three unrelated
// pictures).
constexpr double kNotchCentreHz = 2000.0;
constexpr double kNotchWidthOctaves = 0.35;
constexpr double kNotchDepthDb = 8.0;

// HF rolloff: a waveguide or a horn losing pattern control and level
// together above 5 kHz, down 12 dB by the top of the range.
constexpr double kHfRolloffLowHz = 5000.0;
constexpr double kHfRolloffHighHz = 18000.0;
constexpr double kHfRolloffDepthDb = 12.0;

double magnitudeDb(double hz) {
    const double lf = kLfShelfGainDb * (1.0 - rta::dev::preview::smoothstep(hz, kLfShelfLowHz, kLfShelfHighHz));
    const double notch = rta::dev::preview::logGaussianDb(hz, kNotchCentreHz, kNotchWidthOctaves, kNotchDepthDb);
    const double hf = -kHfRolloffDepthDb * rta::dev::preview::smoothstep(hz, kHfRolloffLowHz, kHfRolloffHighHz);
    return lf + notch + hf;
}

// Phase: a slow overall roll (accumulated group delay of the box + the
// distance the two mics/sources sit apart) plus the arctan swing a
// minimum-phase dip actually produces as frequency crosses it.
constexpr double kGroupDelayDegPerOctave = -22.0;
constexpr double kGroupDelayReferenceHz = 1000.0;
constexpr double kNotchPhaseSwingDeg = 140.0;

double phaseDeg(double hz) {
    const double roll = kGroupDelayDegPerOctave * std::log2(hz / kGroupDelayReferenceHz);
    const double swing =
        rta::dev::preview::logArctanSwingDeg(hz, kNotchCentreHz, kNotchWidthOctaves, -kNotchPhaseSwingDeg);
    return rta::dev::preview::wrapDegrees180(roll + swing);
}

// Coherence: high and flat through the midband, dipping at the LF end
// (low SNR against room noise below ~80 Hz) and again right at the notch
// (a dip is low-energy, so the reference/response ratio there is noisier).
constexpr double kLfCoherenceDipEndHz = 80.0;
constexpr double kLfCoherenceFloor = 0.32;
constexpr double kNotchCoherenceFloor = 0.55;
constexpr double kCoherenceCeiling = 0.97;

double coherence(double hz) {
    const double lfDip = (1.0 - rta::dev::preview::smoothstep(hz, 25.0, kLfCoherenceDipEndHz)) *
                          (kCoherenceCeiling - kLfCoherenceFloor);
    const double notchDip =
        -rta::dev::preview::logGaussianDb(hz, kNotchCentreHz, kNotchWidthOctaves,
                                          kCoherenceCeiling - kNotchCoherenceFloor);
    return std::clamp(kCoherenceCeiling - lfDip - notchDip, 0.03, kCoherenceCeiling);
}

// Delay readout: one decimal ms + whole samples, CLAUDE.md "Reading out
// numbers" applied to a chip instead of an axis label.
constexpr double kDelayMs = 14.6;   // 14.6 ms * 48 kHz = 700.8 -> 701 samples; the
                                    // two figures MUST agree or a reader stops trusting both
constexpr int kDelaySamples = 701;

constexpr int kRibbonHeight = 34;
constexpr int kMagnitudeHeight = 380;
constexpr int kRightMargin = static_cast<int>(rta::view::kFrequencyLabelHalfWidth);

PlotGeometry makeGeometry(juce::Rectangle<int> row, double top, double bottom) {
    PlotGeometry geometry;
    geometry.left = static_cast<float>(row.getX() + rta::view::kLevelLabelWidth);
    geometry.right = static_cast<float>(row.getRight() - kRightMargin);
    geometry.top = static_cast<float>(row.getY());
    geometry.bottom = static_cast<float>(row.getBottom());
    geometry.fLowHz = 20.0;
    geometry.fHighHz = 20000.0;
    geometry.dbTop = top;
    geometry.dbBottom = bottom;
    return geometry;
}

// Whole-degree axis labels, one per 90 deg -- CLAUDE.md's dB-keeps-one-
// decimal rule is a dB rule, not a "every y axis" rule (project CLAUDE.md,
// "Different quantities, different rules -- do not unify them for
// tidiness"), so phase gets its own loop rather than PlotAxes::
// drawLevelLabels's forced one decimal.
void drawPhaseLabels(juce::Graphics& g, const PlotGeometry& geometry) {
    g.setFont(az::ui::monoFont(az::ui::tableFontSize));
    g.setColour(rta::view::axisText);
    for (double deg = geometry.dbTop; deg >= geometry.dbBottom - 1e-6; deg -= 90.0) {
        const float y = geometry.yForDb(deg);
        juce::Rectangle<float> cell(geometry.left - static_cast<float>(rta::view::kLevelLabelWidth), y - 7.0f,
                                     static_cast<float>(rta::view::kLevelLabelWidth) - 6.0f, 14.0f);
        g.drawText(juce::String(static_cast<int>(std::llround(deg))), cell, juce::Justification::centredRight,
                   false);
    }
}

}  // namespace

// No `setSize` here -- the caller (rtatool_snapshot, or eventually the app's
// developer menu) owns dimensions and drives layout by calling `setSize`
// then relying on `resized()`, the same contract every other component in
// this app follows (SpecimenComponent, RtaView).
TransferFunctionPreview::TransferFunctionPreview() = default;

void TransferFunctionPreview::resized() {
    auto area = getLocalBounds().reduced(az::ui::gap * 2);
    mastheadArea_ = area.removeFromTop(az::ui::transportHeight / 2);
    area.removeFromTop(az::ui::gap * 2);

    ribbonArea_ = area.removeFromTop(kRibbonHeight);
    area.removeFromTop(az::ui::gap);

    // The frequency-label strip belongs to the bottom-most pane only; it is
    // trimmed from `area` here, before the magnitude/phase split, so both
    // panes keep an identical top/bottom-free plot height and only
    // `paintPhase` draws into the reserved strip below its own geometry.
    area.removeFromBottom(rta::view::kFrequencyLabelHeight);

    magnitudeArea_ = area.removeFromTop(kMagnitudeHeight);
    area.removeFromTop(az::ui::gap);
    phaseArea_ = area;
}

void TransferFunctionPreview::paint(juce::Graphics& g) {
    g.fillAll(az::ui::background);

    rta::dev::preview::drawMasthead(g, mastheadArea_, "TRANSFER FUNCTION",
                                    "EARLY PREVIEW  --  SYNTHETIC DATA");

    paintRibbon(g, ribbonArea_);
    paintMagnitude(g, magnitudeArea_);
    paintPhase(g, phaseArea_);
}

void TransferFunctionPreview::paintRibbon(juce::Graphics& g, juce::Rectangle<int> area) const {
    const PlotGeometry geometry = makeGeometry(area, 1.0, 0.0);

    g.setColour(az::ui::faded);
    g.setFont(az::ui::legendFont(az::ui::columnFontSize, true, az::ui::trackingColumn));
    juce::Rectangle<int> caption(area.getX(), area.getY(), rta::view::kLevelLabelWidth, area.getHeight());
    g.drawText("COH", caption, juce::Justification::centredRight, false);

    // One thin filled column per pixel, alpha set by coherence(hz) itself --
    // the ribbon's whole point is that trust fades continuously, not in
    // steps, so the fill is computed per column rather than drawn as flat
    // bands.
    const int left = static_cast<int>(geometry.left);
    const int right = static_cast<int>(geometry.right);
    for (int x = left; x < right; ++x) {
        const double hz = geometry.hzForX(static_cast<float>(x) + 0.5f);
        const float trust = static_cast<float>(coherence(hz));
        g.setColour(az::ui::accent.withAlpha(0.15f + 0.65f * trust));
        g.fillRect(x, area.getY(), 1, area.getHeight());
    }

    g.setColour(rta::view::grid);
    g.drawRect(juce::Rectangle<float>(geometry.left, static_cast<float>(area.getY()), geometry.right - geometry.left,
                                       static_cast<float>(area.getHeight())),
               1.0f);
}

void TransferFunctionPreview::paintMagnitude(juce::Graphics& g, juce::Rectangle<int> area) const {
    const PlotGeometry geometry = makeGeometry(area, 18.0, -18.0);

    rta::view::drawGrid(g, geometry);
    rta::view::drawLevelLabels(g, geometry);

    juce::Path path;
    for (float x = geometry.left; x <= geometry.right; x += 1.0f) {
        const double hz = geometry.hzForX(x);
        const float y = geometry.yForDb(magnitudeDb(hz));
        if (x == geometry.left)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }
    g.setColour(rta::view::trace);
    g.strokePath(path, juce::PathStrokeType(2.0f));

    const juce::String delayValue =
        juce::String(kDelayMs, 1) + " ms    " + juce::String(kDelaySamples) + " samples";
    juce::Rectangle<int> chip(static_cast<int>(geometry.right) - 220, static_cast<int>(geometry.top) + az::ui::gap,
                               220, az::ui::fieldHeight * 2);
    rta::dev::preview::drawChip(g, chip, "DELAY", delayValue, rta::view::readoutText);
}

void TransferFunctionPreview::paintPhase(juce::Graphics& g, juce::Rectangle<int> area) const {
    const PlotGeometry geometry = makeGeometry(area, 180.0, -180.0);

    rta::view::drawGrid(g, geometry);
    rta::view::drawFrequencyLabels(g, geometry);
    drawPhaseLabels(g, geometry);

    juce::Path path;
    double previousDeg = 0.0;
    bool haveSubPath = false;
    for (float x = geometry.left; x <= geometry.right; x += 1.0f) {
        const double hz = geometry.hzForX(x);
        const double deg = phaseDeg(hz);
        const float y = geometry.yForDb(deg);

        // A wrap discontinuity (the trace jumping from near +180 to near
        // -180 or back) must lift the pen rather than draw the connecting
        // line straight across the pane -- that line is not part of the
        // curve, it is an artefact of the wrap.
        if (haveSubPath && std::abs(deg - previousDeg) > 180.0) {
            path.startNewSubPath(x, y);
        } else if (!haveSubPath) {
            path.startNewSubPath(x, y);
        } else {
            path.lineTo(x, y);
        }
        previousDeg = deg;
        haveSubPath = true;
    }
    g.setColour(rta::view::trace);
    g.strokePath(path, juce::PathStrokeType(2.0f));
}
