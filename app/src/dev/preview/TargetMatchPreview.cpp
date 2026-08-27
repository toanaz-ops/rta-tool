// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/dev/preview. See
// docs/specs/2026-08-28-interactive-tuning-visuals.md.
#include "dev/preview/TargetMatchPreview.h"

#include "dev/preview/PreviewFurniture.h"
#include "dev/preview/PreviewMath.h"
#include "view/MeasureColours.h"
#include "view/PlotAxes.h"
#include "view/PlotGeometry.h"

#include <az_ui/az_ui.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace {

using rta::view::PlotGeometry;

// ---------------------------------------------------------------------
// Illustrative preview curve: a target already at 0 dB (spec step 1, "auto
// level offset" -- the offset is assumed already applied, so the target
// itself is flat) with a measured trace that has exactly the two problems
// the two suggestion chips below name. Not derived from any real
// measurement.
// ---------------------------------------------------------------------

constexpr double kTargetDb = 0.0;
constexpr double kCorridorDb = 3.0;  // spec step 2 default: +-3 dB.

// Below this, coherence is assumed too low to judge -- spec step 3, "bins
// below the coherence threshold are hatched and excluded from both
// judgement and score". A flat illustrative cutoff stands in for a real
// per-band coherence gate.
constexpr double kLowCoherenceEndHz = 60.0;

// Offender #1: a peak the first suggestion chip corrects exactly (a chip
// reading "-4.0 dB" only means something if the peak really is +4.0 dB).
constexpr double kPeak1Hz = 250.0;
constexpr double kPeak1GainDb = 4.0;
constexpr double kPeak1Q = 2.2;

// Offender #2: a dip, corrected by a boost.
constexpr double kPeak2Hz = 3500.0;
constexpr double kPeak2GainDb = -3.5;
constexpr double kPeak2Q = 3.0;

// A gentle HF settle so the trace does not sit dead flat outside the two
// named problems -- a perfectly flat "problem-free" run either side would
// read as a bug, not a calm region.
constexpr double kHfSettleStartHz = 9000.0;
constexpr double kHfSettleDepthDb = 1.4;

// Q -> gaussian width in octaves: a standard parametric-EQ rule of thumb
// (bandwidth in octaves ~ 1.4 / Q), close enough for an illustrative curve
// that only has to look like a real filter shape, not equal one.
double qToWidthOctaves(double q) { return 1.4 / q; }

double measuredDb(double hz) {
    const double peak1 = rta::dev::preview::logGaussianDb(hz, kPeak1Hz, qToWidthOctaves(kPeak1Q), -kPeak1GainDb);
    const double peak2 = rta::dev::preview::logGaussianDb(hz, kPeak2Hz, qToWidthOctaves(kPeak2Q), -kPeak2GainDb);
    const double lfDrift = 2.6 * (1.0 - rta::dev::preview::smoothstep(hz, 25.0, kLowCoherenceEndHz));
    const double hfSettle = -kHfSettleDepthDb * rta::dev::preview::smoothstep(hz, kHfSettleStartHz, 19000.0);
    return kTargetDb + lfDrift + peak1 + peak2 + hfSettle;
}

// Spec-given example readout (docs/specs/2026-08-28-interactive-tuning-
// visuals.md, V1 point 7): "LF 62   MF 91   HF 84". Illustrative, matched to
// the shape above -- LF is dragged down by the untrusted region it can never
// score points in, MF carries the 250 Hz miss but is otherwise clean, HF has
// the milder 3.5 kHz dip and settle.
constexpr int kScoreLf = 62;
constexpr int kScoreMf = 91;
constexpr int kScoreHf = 84;

juce::Colour scoreColour(int score) {
    if (score >= 85) return rta::view::match;
    if (score >= 70) return rta::view::borderline;
    return rta::view::miss;
}

enum class Judgement { untrusted, match, miss };

Judgement judge(double hz, double deviationDb) {
    if (hz < kLowCoherenceEndHz) return Judgement::untrusted;
    return std::abs(deviationDb) <= kCorridorDb ? Judgement::match : Judgement::miss;
}

juce::Colour judgementColour(Judgement j) {
    switch (j) {
        case Judgement::match: return rta::view::match;
        case Judgement::miss: return rta::view::miss;
        case Judgement::untrusted: return rta::view::untrusted;
    }
    return rta::view::untrusted;
}

constexpr int kRightMargin = static_cast<int>(rta::view::kFrequencyLabelHalfWidth);

PlotGeometry makeGeometry(juce::Rectangle<int> row) {
    PlotGeometry geometry;
    geometry.left = static_cast<float>(row.getX() + rta::view::kLevelLabelWidth);
    geometry.right = static_cast<float>(row.getRight() - kRightMargin);
    geometry.top = static_cast<float>(row.getY());
    geometry.bottom = static_cast<float>(row.getBottom());
    geometry.fLowHz = 20.0;
    geometry.fHighHz = 20000.0;
    geometry.dbTop = 9.0;
    geometry.dbBottom = -9.0;
    return geometry;
}

}  // namespace

TargetMatchPreview::TargetMatchPreview() = default;

void TargetMatchPreview::resized() {
    auto area = getLocalBounds().reduced(az::ui::gap * 2);
    mastheadArea_ = area.removeFromTop(az::ui::transportHeight / 2);
    area.removeFromTop(az::ui::gap * 2);

    scoreChipRow_ = area.removeFromTop(az::ui::fieldHeight * 2);
    area.removeFromTop(az::ui::gap);

    area.removeFromBottom(rta::view::kFrequencyLabelHeight);
    plotArea_ = area;
}

void TargetMatchPreview::paint(juce::Graphics& g) {
    g.fillAll(az::ui::background);

    rta::dev::preview::drawMasthead(g, mastheadArea_, "TARGET MATCH", "EARLY PREVIEW  --  SYNTHETIC DATA");

    const PlotGeometry geometry = makeGeometry(plotArea_);
    rta::view::drawGrid(g, geometry);
    rta::view::drawFrequencyLabels(g, geometry);
    rta::view::drawLevelLabels(g, geometry);

    // The untrusted region is hatched across the FULL pane height, not just
    // under the curve -- "judged by nobody" is a property of the frequency
    // range, not of one trace's excursion (spec: "the view must refuse to
    // judge rather than judge on garbage").
    const float untrustedRight = geometry.xForHz(kLowCoherenceEndHz);
    rta::dev::preview::drawHatch(
        g, juce::Rectangle<float>(geometry.left, geometry.top, untrustedRight - geometry.left,
                                   geometry.bottom - geometry.top),
        rta::view::untrusted);

    // Corridor: a translucent band, judgement is against ITS edges, not the
    // target line (spec step 2).
    const float corridorTop = geometry.yForDb(kTargetDb + kCorridorDb);
    const float corridorBottom = geometry.yForDb(kTargetDb - kCorridorDb);
    g.setColour(rta::view::target.withAlpha(0.14f));
    g.fillRect(juce::Rectangle<float>(geometry.left, corridorTop, geometry.right - geometry.left,
                                       corridorBottom - corridorTop));

    g.setColour(rta::view::target);
    const float targetY = geometry.yForDb(kTargetDb);
    g.drawDashedLine(juce::Line<float>(geometry.left, targetY, geometry.right, targetY),
                     std::array<float, 2>{ 5.0f, 4.0f }.data(), 2, 1.5f);

    // The measured trace, drawn as one path segment per judgement run so
    // each run can carry its own colour -- a single path stroked once could
    // only ever have one colour, and the whole point of this view is that it
    // does not.
    juce::Path segment;
    Judgement segmentJudgement = Judgement::untrusted;
    bool haveSegment = false;
    auto flushSegment = [&] {
        if (haveSegment) {
            g.setColour(judgementColour(segmentJudgement));
            g.strokePath(segment, juce::PathStrokeType(2.2f));
        }
        segment.clear();
        haveSegment = false;
    };

    for (float x = geometry.left; x <= geometry.right; x += 1.0f) {
        const double hz = geometry.hzForX(x);
        const double deviation = measuredDb(hz);
        const float y = geometry.yForDb(deviation);
        const Judgement j = judge(hz, deviation);

        if (!haveSegment || j != segmentJudgement) {
            if (haveSegment) segment.lineTo(x, y);  // share the boundary point
            flushSegment();
            segment.startNewSubPath(x, y);
            segmentJudgement = j;
            haveSegment = true;
        } else {
            segment.lineTo(x, y);
        }
    }
    flushSegment();

    paintScoreChips(g);
    paintSuggestionChips(g);
}

void TargetMatchPreview::paintScoreChips(juce::Graphics& g) const {
    auto row = scoreChipRow_;
    const int chipWidth = 96;  // sized to the number, not to spare air
    auto place = [&](const char* legend, int score) {
        auto chip = row.removeFromRight(chipWidth);
        row.removeFromRight(az::ui::gap);
        rta::dev::preview::drawChip(g, chip, legend, juce::String(score), scoreColour(score),
                                    az::ui::countdownFontSize);
    };
    // Right to left so the row reads LF, MF, HF left-to-right on screen.
    place("HF", kScoreHf);
    place("MF", kScoreMf);
    place("LF", kScoreLf);
}

void TargetMatchPreview::paintSuggestionChips(juce::Graphics& g) const {
    const PlotGeometry geometry = makeGeometry(plotArea_);
    constexpr int kChipWidth = 210;
    constexpr int kChipHeight = 40;

    auto placeNear = [&](double hz, double gainDb, double q, bool above) {
        const float x = geometry.xForHz(hz);
        const int chipX = juce::jlimit(static_cast<int>(geometry.left), static_cast<int>(geometry.right) - kChipWidth,
                                       static_cast<int>(x) - kChipWidth / 2);
        const int chipY = above ? static_cast<int>(geometry.top) + az::ui::gap
                                 : static_cast<int>(geometry.bottom) - kChipHeight - az::ui::gap;
        // Whole hertz, never a k abbreviation -- CLAUDE.md "Reading out numbers".
        const juce::String value = juce::String(hz, 0) + " Hz" +
                                   "  " + (gainDb >= 0.0 ? "+" : juce::String()) + juce::String(gainDb, 1) + " dB  Q " +
                                   juce::String(q, 1);
        rta::dev::preview::drawChip(g, juce::Rectangle<int>(chipX, chipY, kChipWidth, kChipHeight), "PEQ", value,
                                    rta::view::readoutText);
    };

    // Suggestion is the CORRECTION, so it carries the opposite sign of the
    // problem it fixes (spec V1 point 6, manual-mode "ranked max-3
    // suggestions for engineers who tune by hand").
    placeNear(kPeak1Hz, -kPeak1GainDb, kPeak1Q, true);
    placeNear(kPeak2Hz, -kPeak2GainDb, kPeak2Q, false);
}
