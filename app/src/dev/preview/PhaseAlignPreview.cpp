// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/dev/preview. See
// docs/specs/2026-08-28-interactive-tuning-visuals.md and the G18 record
// docs/dsp/2026-09-06-l7-alignment-wizard.md Sec.6.
#include "dev/preview/PhaseAlignPreview.h"

#include "dev/preview/PreviewFurniture.h"
#include "dev/preview/PreviewMath.h"
#include "trace/Trace.h"
#include "view/AxisMetrics.h"
#include "view/CrossoverSurfaceRenderer.h"
#include "view/MeasureColours.h"
#include "view/PlotGeometry.h"

#include <az_ui/az_ui.h>

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
    // Grid, shaded fit window, target line and the relative-phase curve
    // itself: identical code the live XOVER pane paints with
    // (view/CrossoverSurfaceRenderer.h). Only the chip below is specific to
    // this specimen -- it names a topology (BW4) this preview's constructor
    // chose, which CrossoverSurface itself never hands back out.
    rta::view::paintCrossoverPhase(g, area, surface_);

    const PlotGeometry geometry = rta::view::crossoverPhaseGeometry(area);
    const double targetDeg = surface_.targetRadians() * 180.0 / kPi;
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
    rta::view::paintCrossoverSummation(g, area, surface_);
}
