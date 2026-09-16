// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/dev/preview. See
// docs/specs/2026-08-28-interactive-tuning-visuals.md and the G18 record
// docs/dsp/2026-09-06-l7-alignment-wizard.md Sec.6.
#include "dev/preview/PhaseAlignPreview.h"

#include "dev/preview/PreviewFurniture.h"
#include "dev/preview/PreviewMath.h"
#include "trace/Trace.h"
#include "view/MeasureColours.h"
#include "view/PlotAxes.h"
#include "view/PlotGeometry.h"

#include <az_ui/az_ui.h>

#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace {

using rta::view::PlotGeometry;

constexpr double kPi = rta::dev::preview::kPi;
constexpr double kSampleRate = 48000.0;
/// 32768 at 48 kHz is a 1.46 Hz bin. A 1024-point transform was tried first
/// and rendered the 100 Hz crossover with two bins across it -- the curves came
/// out as straight segments between three points, which is a picture of the
/// FFT size and not of the crossover. A specimen has to be resolved where its
/// subject is, and its subject is down low.
constexpr int kFftSize = 32768;
constexpr double kCrossoverHz = 100.0;
constexpr int kOrder = 4;

/// The delay the sub is measured with, and the delay the operator is
/// PREVIEWING on the main to compensate it. Two numbers that are the same
/// number on purpose: the picture's whole subject is a pending G11 op
/// turning the ghost into the prediction.
/// 4 ms is 144 degrees at the 100 Hz crossover -- deep into cancellation, so
/// the ghost and the prediction are unmistakably two different curves rather
/// than two readings of the same one.
constexpr double kSubArrivalDelaySeconds = 0.0040;

/// Butterworth prototype, from the POLE formula rather than a polynomial:
/// D_N(s) = prod_k (s - p_k), p_k = exp(j*pi*(2k + N + 1) / (2N)). The
/// low-pass is 1/D_N and the matching high-pass is s^N/D_N, so H/L = s^N and
/// arg H - arg L = N*90 degrees at EVERY frequency -- which is exactly the
/// identity the target line in the pane below is drawn at.
std::complex<double> butterworthDenominator(std::complex<double> s) {
    std::complex<double> denominator{ 1.0, 0.0 };
    for (int k = 0; k < kOrder; ++k) {
        const double angle = kPi * (2.0 * k + kOrder + 1.0) / (2.0 * kOrder);
        denominator *= (s - std::polar(1.0, angle));
    }
    return denominator;
}

/// One side of the pair as a stored Trace: dB and wrapped radians, the shape a
/// capture really arrives in, so the specimen exercises VirtualTrace's
/// conversion the same way the wizard does.
rta::trace::Trace makeSide(const char* id, bool highPass, double extraDelaySeconds) {
    rta::trace::CaptureMeta meta;
    meta.id = id;
    meta.sampleRate = kSampleRate;
    meta.fftSize = kFftSize;
    meta.channelRoles = "ref:1 meas:2";

    const std::size_t points = rta::trace::pointCountFor(kFftSize);
    const double binWidthHz = kSampleRate / static_cast<double>(kFftSize);
    std::vector<float> magnitudeDb(points);
    std::vector<float> phase(points);
    for (std::size_t k = 0; k < points; ++k) {
        const double hz = static_cast<double>(k) * binWidthHz;
        const std::complex<double> s{ 0.0, hz / kCrossoverHz };
        const std::complex<double> denominator = butterworthDenominator(s);
        std::complex<double> h = 1.0 / denominator;
        if (highPass) h = std::pow(s, kOrder) / denominator;
        h *= std::polar(1.0, -2.0 * kPi * hz * extraDelaySeconds);

        const double magnitude = std::max(std::abs(h), 1.0e-6);
        magnitudeDb[k] = static_cast<float>(std::max(20.0 * std::log10(magnitude), -120.0));
        phase[k] = static_cast<float>(std::arg(h));
    }

    auto trace = rta::trace::Trace::make(std::move(meta), std::move(magnitudeDb));
    jassert(trace.has_value());
    [[maybe_unused]] const bool phaseAccepted = trace->setPhase(std::move(phase));
    jassert(phaseAccepted);
    return std::move(*trace);
}

rta::trace::VirtualTrace virtualOf(const rta::trace::Trace& trace) {
    auto result = rta::trace::VirtualTrace::fromTrace(trace);
    jassert(result.has_value());
    return std::move(*result);
}

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

/// Whole degrees, not the dB view's forced one decimal: different quantities,
/// different rules (CLAUDE.md "Reading out numbers").
void drawDegreeLabels(juce::Graphics& g, const PlotGeometry& geometry) {
    g.setFont(az::ui::monoFont(az::ui::tableFontSize));
    g.setColour(rta::view::axisText);
    for (double deg = geometry.dbTop; deg >= geometry.dbBottom - 1e-6; deg -= 90.0) {
        const float y = geometry.yForDb(deg);
        juce::Rectangle<float> cell(geometry.left
                                        - static_cast<float>(rta::view::kLevelLabelWidth),
                                    y - 7.0f,
                                    static_cast<float>(rta::view::kLevelLabelWidth) - 6.0f,
                                    14.0f);
        g.drawText(juce::String(static_cast<int>(std::llround(deg))), cell,
                   juce::Justification::centredRight, false);
    }
}

/// Strokes a dB series over the log-frequency axis, one bin per point.
void strokeSeries(juce::Graphics& g, const PlotGeometry& geometry,
                  const std::vector<float>& db, double binWidthHz, juce::Colour colour,
                  float thickness, bool dashed = false) {
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

PhaseAlignPreview::PhaseAlignPreview() {
    surface_.setAskedTopology({ rta::measure::CrossoverFamily::Butterworth, kOrder },
                              rta::measure::ProcessorInversion::No);
    surface_.setWindow({ kCrossoverHz, 1.0 });
    // A = the HIGH-PASS side, B = the LOW-PASS side (ALIGN-R14). The low-pass
    // side carries the extra arrival delay; the high-pass side carries none.
    surface_.setSources(virtualOf(makeSide("main-hp", true, 0.0)),
                        virtualOf(makeSide("sub-lp", false, kSubArrivalDelaySeconds)));
    // The pending G11 op: delay the EARLIER side by what the later one costs.
    // The ghost was captured before this line ran, so the two summed curves in
    // the lower pane are "before" and "after" of one operator decision.
    surface_.setPendingOps({ rta::trace::DelayOp{ kSubArrivalDelaySeconds } }, {});
}

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
    rta::dev::preview::drawMasthead(g, mastheadArea_, "PHASE ALIGNMENT",
                                     "EARLY PREVIEW  --  SYNTHETIC BW4 PAIR");
    paintRelativePhase(g, phaseArea_);
    paintSummation(g, summationArea_);
}

void PhaseAlignPreview::paintRelativePhase(juce::Graphics& g, juce::Rectangle<int> area) const {
    const PlotGeometry geometry = makeGeometry(area, 180.0, -180.0);

    // The fit window, washed under the grid: this is the stretch of spectrum
    // the relative-phase trace is defined on, and the only one the verdict
    // reads (record Sec.4).
    const auto& relative = surface_.relativePhase();
    if (!relative.empty()) {
        const float left = geometry.xForHz(relative.front().hz);
        const float right = geometry.xForHz(relative.back().hz);
        g.setColour(az::ui::accent.withAlpha(0.07f));
        g.fillRect(juce::Rectangle<float>(left, geometry.top, right - left,
                                          geometry.bottom - geometry.top));
    }

    rta::view::drawGrid(g, geometry);
    rta::view::drawFrequencyLabels(g, geometry);
    drawDegreeLabels(g, geometry);

    // THE ASKED LINE. Horizontal, at the offset record Sec.3 predicts for the
    // topology the operator NAMED -- nothing on this plot derived it.
    const double targetDeg = surface_.targetRadians() * 180.0 / kPi;
    const float targetY = geometry.yForDb(targetDeg);
    g.setColour(rta::view::target);
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
    g.setColour(rta::view::match);
    g.strokePath(path, juce::PathStrokeType(2.0f));

    const juce::String legend =
        "BW" + juce::String(kOrder) + "   ASKED TARGET "
        + juce::String(static_cast<int>(std::llround(targetDeg))) + " deg";
    juce::Rectangle<int> chip(static_cast<int>(geometry.right) - 320,
                              static_cast<int>(geometry.top) + az::ui::gap, 320,
                              az::ui::fieldHeight * 2);
    rta::dev::preview::drawChip(g, chip, "TOPOLOGY -- ASKED, NEVER INFERRED", legend,
                                 rta::view::readoutText);
}

void PhaseAlignPreview::paintSummation(juce::Graphics& g, juce::Rectangle<int> area) const {
    const PlotGeometry geometry = makeGeometry(area, 12.0, -24.0);
    const double binWidthHz = surface_.binWidthHz();

    rta::view::drawGrid(g, geometry);
    rta::view::drawFrequencyLabels(g, geometry);
    rta::view::drawLevelLabels(g, geometry);

    // MARKS, not criteria: +6.02 dB for two coherent sources, and what this
    // topology is designed to sum to at fc.
    for (const double markDb : { surface_.marks().coherentSumDb, surface_.marks().designedSumDb }) {
        const float y = geometry.yForDb(markDb);
        g.setColour(rta::view::target.withAlpha(0.55f));
        g.fillRect(geometry.left, y - 0.5f, geometry.right - geometry.left, 1.0f);
        // Labelled at the RIGHT edge: the left is where the legend sits, and a
        // mark label under a legend reads as part of it.
        g.setFont(az::ui::monoFont(az::ui::tableFontSize));
        g.drawText(juce::String(markDb, 1) + " dB",
                   juce::Rectangle<float>(geometry.right - 92.0f, y - 14.0f, 88.0f, 12.0f),
                   juce::Justification::centredRight, false);
    }

    strokeSeries(g, geometry, surface_.highSideDb(), binWidthHz, rta::view::trace, 1.4f);
    strokeSeries(g, geometry, surface_.lowSideDb(), binWidthHz, rta::view::secondaryTrace, 1.4f);
    strokeSeries(g, geometry, surface_.ghostSumDb(), binWidthHz, rta::view::ghost, 1.8f, true);
    strokeSeries(g, geometry, surface_.predictedSumDb(), binWidthHz, rta::view::match, 2.2f);

    g.setColour(az::ui::faded);
    g.setFont(az::ui::legendFont(az::ui::columnFontSize, true, az::ui::trackingColumn));
    g.drawText("PREDICTED SUM (SOLID)   PRE-ALIGNMENT GHOST (DASHED)",
               juce::Rectangle<int>(static_cast<int>(geometry.left) + az::ui::gap,
                                     static_cast<int>(geometry.top) + az::ui::spacing, 420,
                                     az::ui::captionHeight),
               juce::Justification::centredLeft, false);
}
