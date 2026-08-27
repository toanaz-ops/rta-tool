// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/dev/preview. See
// docs/specs/2026-08-28-interactive-tuning-visuals.md.
#include "dev/preview/PhaseAlignPreview.h"

#include "dev/preview/PreviewFurniture.h"
#include "dev/preview/PreviewMath.h"
#include "view/MeasureColours.h"
#include "view/PlotAxes.h"
#include "view/PlotGeometry.h"

#include <az_ui/az_ui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

namespace {

using rta::view::PlotGeometry;

// ---------------------------------------------------------------------
// Illustrative preview curves: two phase traces built so their DIFFERENCE
// (deltaPhiDeg below) is designed directly, rather than hoping two
// independently-sweeping curves happen to converge somewhere plausible.
// Not derived from any real measurement.
// ---------------------------------------------------------------------

// Trace A (e.g. the sub, or capture 1): a slow, uneventful roll -- this
// preview's whole point is the RELATIONSHIP between the two traces, so
// trace A carries no notch of its own to distract from it.
constexpr double kTraceARollDegPerOctave = -10.0;
constexpr double kTraceAReferenceHz = 100.0;

double traceARawDeg(double hz) {
    return kTraceARollDegPerOctave * std::log2(hz / kTraceAReferenceHz);
}

// The crossover region the engineer selected -- spec V2 point 1: "judged
// only in the user-selected crossover region, because whole-band phase
// match is neither achievable nor needed".
constexpr double kCrossoverLowHz = 80.0;
constexpr double kCrossoverHighHz = 160.0;
constexpr double kCrossoverCentreHz = 113.0;  // sqrt(80 * 160), the band's own geometric centre.
constexpr double kCrossoverWidthOctaves = 0.55;

// Delta-phi: small (well inside the default 45 deg threshold) at the
// crossover centre -- this preview depicts AUTO DELAY already applied, so
// the two traces converge there -- and large away from it, where whole-band
// match was never the goal.
constexpr double kMinMisalignDeg = 15.0;
constexpr double kMaxMisalignDeg = 175.0;  // just short of full cancellation, so the
                                            // summation ghost's null stays visibly a
                                            // deep dip rather than an unreadable spike
                                            // to -infinity dB.
constexpr double kThresholdDeg = 45.0;     // spec default.

double deltaPhiDeg(double hz) {
    return kMaxMisalignDeg + rta::dev::preview::logGaussianDb(hz, kCrossoverCentreHz, kCrossoverWidthOctaves,
                                                              kMaxMisalignDeg - kMinMisalignDeg);
}

double traceBRawDeg(double hz) { return traceARawDeg(hz) + deltaPhiDeg(hz); }

// AUTO DELAY solver readout (spec V2 point 2): one decimal ms, whole
// samples' worth of confidence implied by the number alone here since this
// is a mockup chip, not a live solve. "polarity --" reads as "not flipped" --
// an ASCII double-hyphen stands in for the spec text's em dash, which this
// toolchain's embedded faces are not guaranteed to carry a glyph for.
constexpr double kSolvedDelayMs = 3.3;  // one-decimal ms, the record's own convention
constexpr int kSolvedDelaySamples = 158;    // 3.3 ms * 48 kHz = 158.4 -> 158
constexpr int kSolvedScore = 87;

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

// Same reasoning as TransferFunctionPreview.cpp's drawPhaseLabels: whole
// degrees, not the dB view's forced one decimal (CLAUDE.md, different
// quantities need different rules) -- duplicated rather than shared because
// the two call sites are the only ones and a header for one twelve-line
// function would cost more to read than it saves.
void drawDegreeLabels(juce::Graphics& g, const PlotGeometry& geometry) {
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

// Draws a wrapped phase trace, lifting the pen at wrap discontinuities --
// identical technique to TransferFunctionPreview.cpp's phase pane, applied
// here to two traces instead of one.
void strokeWrappedPhase(juce::Graphics& g, const PlotGeometry& geometry, juce::Colour colour,
                        const std::function<double(double)>& rawDegFn) {
    juce::Path path;
    double previousDeg = 0.0;
    bool haveSubPath = false;
    for (float x = geometry.left; x <= geometry.right; x += 1.0f) {
        const double hz = geometry.hzForX(x);
        const double deg = rta::dev::preview::wrapDegrees180(rawDegFn(hz));
        const float y = geometry.yForDb(deg);

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
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(2.0f));
}

}  // namespace

PhaseAlignPreview::PhaseAlignPreview() = default;

void PhaseAlignPreview::resized() {
    auto area = getLocalBounds().reduced(az::ui::gap * 2);
    mastheadArea_ = area.removeFromTop(az::ui::transportHeight / 2);
    area.removeFromTop(az::ui::gap * 2);

    constexpr int kSummationHeight = 200;
    area.removeFromBottom(rta::view::kFrequencyLabelHeight);

    summationArea_ = area.removeFromBottom(kSummationHeight);
    area.removeFromBottom(az::ui::gap);
    phaseArea_ = area;
}

void PhaseAlignPreview::paint(juce::Graphics& g) {
    g.fillAll(az::ui::background);

    rta::dev::preview::drawMasthead(g, mastheadArea_, "PHASE ALIGNMENT", "EARLY PREVIEW  --  SYNTHETIC DATA");

    paintPhase(g, phaseArea_);
    paintSummationGhost(g, summationArea_);
}

void PhaseAlignPreview::paintPhase(juce::Graphics& g, juce::Rectangle<int> area) const {
    const PlotGeometry geometry = makeGeometry(area, 180.0, -180.0);

    // The crossover-region wash goes down FIRST, under the grid and traces,
    // so it reads as "the region under focus" rather than an occlusion.
    const float regionLeft = geometry.xForHz(kCrossoverLowHz);
    const float regionRight = geometry.xForHz(kCrossoverHighHz);
    g.setColour(az::ui::accent.withAlpha(0.07f));
    g.fillRect(juce::Rectangle<float>(regionLeft, geometry.top, regionRight - regionLeft,
                                       geometry.bottom - geometry.top));

    // Delta-phi-under-threshold shading: green where the crossover-region
    // AND under-threshold conditions both hold (spec V2 point 1) -- drawn as
    // per-column fill, like the coherence ribbon, because "in or out" is a
    // per-frequency verdict, not a shape that fits one flat rectangle once
    // the threshold's edges are curved in frequency.
    const int left = static_cast<int>(geometry.left);
    const int right = static_cast<int>(geometry.right);
    for (int x = left; x < right; ++x) {
        const double hz = geometry.hzForX(static_cast<float>(x) + 0.5f);
        if (hz < kCrossoverLowHz || hz > kCrossoverHighHz) continue;
        if (deltaPhiDeg(hz) >= kThresholdDeg) continue;
        g.setColour(rta::view::match.withAlpha(0.16f));
        g.fillRect(x, static_cast<int>(geometry.top), 1, area.getHeight());
    }

    rta::view::drawGrid(g, geometry);
    rta::view::drawFrequencyLabels(g, geometry);
    drawDegreeLabels(g, geometry);

    strokeWrappedPhase(g, geometry, rta::view::trace, &traceARawDeg);
    strokeWrappedPhase(g, geometry, rta::view::secondaryTrace, &traceBRawDeg);

    const juce::String solverValue = "+" + juce::String(kSolvedDelayMs, 1) + " ms   " + juce::String(kSolvedDelaySamples) + " samples   polarity --   score " +
                                     juce::String(kSolvedScore);
    juce::Rectangle<int> chip(static_cast<int>(geometry.right) - 300, static_cast<int>(geometry.top) + az::ui::gap,
                               300, az::ui::fieldHeight * 2);
    rta::dev::preview::drawChip(g, chip, "AUTO DELAY", solverValue, rta::view::match);
}

void PhaseAlignPreview::paintSummationGhost(juce::Graphics& g, juce::Rectangle<int> area) const {
    const PlotGeometry geometry = makeGeometry(area, 6.0, -18.0);

    const float regionLeft = geometry.xForHz(kCrossoverLowHz);
    const float regionRight = geometry.xForHz(kCrossoverHighHz);
    g.setColour(az::ui::accent.withAlpha(0.07f));
    g.fillRect(juce::Rectangle<float>(regionLeft, geometry.top, regionRight - regionLeft,
                                       geometry.bottom - geometry.top));

    rta::view::drawGrid(g, geometry);
    rta::view::drawFrequencyLabels(g, geometry);
    rta::view::drawLevelLabels(g, geometry);

    g.setColour(az::ui::faded);
    g.setFont(az::ui::legendFont(az::ui::columnFontSize, true, az::ui::trackingColumn));
    g.drawText("PREDICTED SUMMATION", juce::Rectangle<int>(static_cast<int>(geometry.left) + az::ui::gap,
                                                            static_cast<int>(geometry.top) + az::ui::spacing, 260,
                                                            az::ui::captionHeight),
               juce::Justification::centredLeft, false);

    // |1 + e^{i*deltaPhi}| = 2 * |cos(deltaPhi / 2)| for two unity-level
    // sources -- the textbook two-source summation identity, evaluated at
    // the SAME deltaPhiDeg(hz) the phase pane above already draws, so the
    // ghost is honestly "what this picture's own numbers predict", not an
    // unrelated illustration (spec V2 point 3: "live preview of the combined
    // magnitude at the current delay/polarity ... including the dreaded
    // combing when it's wrong").
    juce::Path path;
    for (float x = geometry.left; x <= geometry.right; x += 1.0f) {
        const double hz = geometry.hzForX(x);
        const double deltaRad = deltaPhiDeg(hz) * (rta::dev::preview::kPi / 180.0);
        const double magnitude = 2.0 * std::abs(std::cos(deltaRad / 2.0));
        const double magnitudeDb = 20.0 * std::log10(std::max(magnitude, 1.0e-4));
        const float y = geometry.yForDb(magnitudeDb);
        if (x == geometry.left)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }
    g.setColour(rta::view::ghost);
    juce::PathStrokeType stroke(1.8f);
    juce::Path dashed;
    const std::array<float, 2> dashLengths{ 6.0f, 4.0f };
    stroke.createDashedStroke(dashed, path, dashLengths.data(), 2);
    g.fillPath(dashed);
}
