// SPDX-License-Identifier: AGPL-3.0-or-later
//
// SWEEP lane task T3 (docs/plans/2026-09-29-sweep-ir-lane-plan.md), rows (a),
// (c), (d), (e), (f). Row (b), the derived tolerance, is test_sweep_ir_tolerance.cpp.
// JUCE-free. A 2 s sweep keeps every case here to a few hundred milliseconds;
// the derived-tolerance test is the one that runs the shipped 10 s configuration.
#include <catch2/catch_test_macros.hpp>

#include "SweepIrFixture.h"
#include "measure/SweepIrPipeline.h"
#include "rta/ir/Deconvolver.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using rta::measure::analyseSweepCapture;
using rta::measure::SweepCapture;
using rta::measure::SweepIrOutcome;
using rta::measure::SweepIrRequest;
using rta::measure::SweepIrStatus;
namespace fx = sweep_fixture;

namespace {

constexpr std::size_t kPreRoll = 12345;  // not a multiple of anything: a stale origin cannot hide

SweepIrRequest shortRequest() {
    SweepIrRequest request;
    request.sweep = rta::measure::shippedSweepConfig(48000.0);
    request.sweep.durationSec = 2.0;
    request.tailSeconds = 0.6;
    return request;
}

// Index of the largest |x| and its signed value.
std::size_t argMaxAbs(const std::vector<float>& x) {
    std::size_t best = 0;
    for (std::size_t i = 1; i < x.size(); ++i) {
        if (std::fabs(x[i]) > std::fabs(x[best])) best = i;
    }
    return best;
}

}  // namespace

// --- (a) pure delay -----------------------------------------------------------

TEST_CASE("a pure delay of 37 samples reads 37 from the relabelled origin, positive",
          "[sweep_ir_pipeline]") {
    const auto request = shortRequest();
    const auto capture = fx::delayCapture(request.sweep, kPreRoll,
                                          rta::measure::requiredTailSamples(request), 37);
    const auto outcome = analyseSweepCapture(capture, request);
    INFO(outcome.message);
    REQUIRE(outcome.status == SweepIrStatus::Ok);

    // Read the peak off the stored samples independently of the pipeline's own
    // `delaySamples`. Without the re-reference, originIndex is the deconvolver's
    // (Ninv - 1) and this reads kPreRoll + 37.
    const std::size_t peak = argMaxAbs(outcome.ir.samples);
    CHECK(static_cast<long>(peak) - static_cast<long>(outcome.ir.originIndex) == 37);
    CHECK(outcome.ir.samples[peak] > 0.0f);  // a delay, not an inversion
    CHECK(outcome.delaySamples == 37);
    CHECK(outcome.referenceArrivalSamples == kPreRoll);
}

// --- (c) tail ----------------------------------------------------------------

TEST_CASE("a tail one sample short is refused and the exact tail is accepted",
          "[sweep_ir_pipeline]") {
    const auto request = shortRequest();
    const auto required = rta::measure::requiredTailSamples(request);
    REQUIRE(required == 28800);  // 0.6 s at 48 kHz, written out so a wrong rounding shows

    const auto shortBy1 = analyseSweepCapture(
        fx::delayCapture(request.sweep, kPreRoll, required - 1, 37), request);
    CHECK(shortBy1.status == SweepIrStatus::TailTooShort);

    const auto exact = analyseSweepCapture(
        fx::delayCapture(request.sweep, kPreRoll, required, 37), request);
    INFO(exact.message);
    CHECK(exact.status == SweepIrStatus::Ok);
}

// --- (d) reference not found ---------------------------------------------------

TEST_CASE("an inverted reference is ReferenceNotFound", "[sweep_ir_pipeline]") {
    const auto request = shortRequest();
    auto capture = fx::delayCapture(request.sweep, kPreRoll,
                                    rta::measure::requiredTailSamples(request), 37);
    for (auto& x : capture.reference) x = -x;
    // The inverted loop's deconvolution peaks NEGATIVE (the analysis pulse is
    // positive by construction); re-referencing to its largest positive value
    // would put t = 0 on a sidelobe and every delay would be wrong by a lot.
    CHECK(analyseSweepCapture(capture, request).status == SweepIrStatus::ReferenceNotFound);
}

TEST_CASE("silence and white noise are not a reference", "[sweep_ir_pipeline]") {
    const auto request = shortRequest();
    auto capture = fx::delayCapture(request.sweep, kPreRoll,
                                    rta::measure::requiredTailSamples(request), 37);
    const auto realReference = capture.reference;

    std::fill(capture.reference.begin(), capture.reference.end(), 0.0f);
    CHECK(analyseSweepCapture(capture, request).status == SweepIrStatus::ReferenceNotFound);

    // A deterministic LCG, so the case is the same on every run and OS.
    std::uint32_t state = 12345u;
    for (auto& x : capture.reference) {
        state = state * 1664525u + 1013904223u;
        x = (static_cast<float>(state >> 8) / 8388608.0f - 1.0f) * 0.25f;
    }
    CHECK(analyseSweepCapture(capture, request).status == SweepIrStatus::ReferenceNotFound);

    // The margin the gate leaves: the real loop's pulse over the deconvolution's
    // rms. The pipeline refuses below 20; this test insists the real figure is
    // at least double that, so the gate cannot be refusing a good loop by a hair.
    const rta::gen::Sweep sweep(request.sweep);
    rta::ir::DeconvolverConfig config;
    config.sampleRate = 48000.0;
    const auto loop = rta::ir::deconvolve(realReference, sweep.buildInverseFilter(), config);
    double sumSquares = 0.0;
    for (const float x : loop.samples) sumSquares += static_cast<double>(x) * x;
    const double rms = std::sqrt(sumSquares / static_cast<double>(loop.samples.size()));
    const double ratio = std::fabs(static_cast<double>(loop.samples[argMaxAbs(loop.samples)])) / rms;
    std::printf("[sweep_ir_pipeline] real loop peak/rms = %.1f (gate 20)\n", ratio);
    CHECK(ratio > 40.0);
}

TEST_CASE("a silent measurement is MeasurementNotFound and a short capture CaptureTooShort",
          "[sweep_ir_pipeline]") {
    const auto request = shortRequest();
    auto capture = fx::delayCapture(request.sweep, kPreRoll,
                                    rta::measure::requiredTailSamples(request), 37);
    auto silent = capture;
    std::fill(silent.measurement.begin(), silent.measurement.end(), 0.0f);
    CHECK(analyseSweepCapture(silent, request).status == SweepIrStatus::MeasurementNotFound);

    capture.reference.resize(1000);
    capture.measurement.resize(1000);
    CHECK(analyseSweepCapture(capture, request).status == SweepIrStatus::CaptureTooShort);

    auto mismatched = fx::delayCapture(request.sweep, kPreRoll,
                                       rta::measure::requiredTailSamples(request), 37);
    mismatched.measurement.pop_back();
    CHECK(analyseSweepCapture(mismatched, request).status == SweepIrStatus::CaptureTooShort);
}

// --- (e) arrival beyond the window ----------------------------------------------

TEST_CASE("an arrival past N - leadIn - 0.1 s is refused, the last one that fits is not",
          "[sweep_ir_pipeline]") {
    const auto request = shortRequest();
    const rta::gen::Sweep sweep(request.sweep);
    const double fs = 48000.0;
    // The bound from the plan's sentence, computed here rather than read from
    // the pipeline: N - leadIn - ceil(0.1 fs), with N = 32768 for a 2 s sweep
    // at 48 kHz (f_lo ~ 80 Hz, leadIn = 1200).
    const auto leadIn = static_cast<std::size_t>(std::llround(2.0 * fs / sweep.validBandLowHz()));
    const std::size_t fftSize = 32768;
    REQUIRE(rta::measure::sweepIrWindowSize(fs, sweep.validBandLowHz()) == fftSize);
    const std::size_t maxDelay = fftSize - leadIn - static_cast<std::size_t>(std::ceil(0.1 * fs));

    const auto required = rta::measure::requiredTailSamples(request);
    const auto fits = analyseSweepCapture(
        fx::delayCapture(request.sweep, kPreRoll, required + maxDelay + 1, maxDelay), request);
    INFO(fits.message);
    CHECK(fits.status == SweepIrStatus::Ok);

    const auto beyond = analyseSweepCapture(
        fx::delayCapture(request.sweep, kPreRoll, required + maxDelay + 2, maxDelay + 1), request);
    CHECK(beyond.status == SweepIrStatus::ArrivalOutOfWindow);
}

TEST_CASE("an arrival earlier than the lead-in before the loop is refused", "[sweep_ir_pipeline]") {
    const auto request = shortRequest();
    const rta::gen::Sweep sweep(request.sweep);
    const auto leadIn = static_cast<std::size_t>(std::llround(2.0 * 48000.0 / sweep.validBandLowHz()));
    auto capture = fx::delayCapture(request.sweep, kPreRoll,
                                    rta::measure::requiredTailSamples(request), 0);
    const std::size_t advance = leadIn + 1;
    // Measurement = reference advanced (read from the future): its peak lands
    // before the window's first sample.
    for (std::size_t i = 0; i + advance < capture.measurement.size(); ++i) {
        capture.measurement[i] = capture.reference[i + advance];
    }
    CHECK(analyseSweepCapture(capture, request).status == SweepIrStatus::ArrivalOutOfWindow);

    // One sample less advance, and the peak sits exactly on the window start.
    capture.measurement = capture.reference;
    for (std::size_t i = 0; i + leadIn < capture.measurement.size(); ++i) {
        capture.measurement[i] = capture.reference[i + leadIn];
    }
    CHECK(analyseSweepCapture(capture, request).status == SweepIrStatus::Ok);
}

// --- (f) the gain applied is the loop's in-band mean ------------------------------

TEST_CASE("the gain applied equals inBandNormalisation of the loop's own deconvolution",
          "[sweep_ir_pipeline]") {
    const auto request = shortRequest();
    const rta::gen::Sweep sweep(request.sweep);
    const auto capture = fx::delayCapture(request.sweep, kPreRoll,
                                          rta::measure::requiredTailSamples(request), 0);
    const auto outcome = analyseSweepCapture(capture, request);
    REQUIRE(outcome.status == SweepIrStatus::Ok);

    // The same two calls the plan's row names: deconvolve the loop raw, then
    // inBandNormalisation over the sweep's valid band.
    rta::ir::DeconvolverConfig config;
    config.sampleRate = 48000.0;
    config.harmonicSpacingL = sweep.lengthConstantL();
    const auto inverse = sweep.buildInverseFilter();
    const auto loop = rta::ir::deconvolve(capture.reference, inverse, config);
    const double expected =
        rta::ir::inBandNormalisation(loop, sweep.validBandLowHz(), sweep.validBandHighHz());

    CHECK(outcome.ir.normalisationGain == expected);
    CHECK(expected != 1.0);  // a gain of 1 would pass trivially if the loop were already unity

    // The measurement here IS the loop, so its stored peak is the loop's peak
    // times the float gain, operation for operation.
    const std::size_t peak = argMaxAbs(loop.samples);
    CHECK(outcome.ir.samples[peak] == loop.samples[peak] * static_cast<float>(expected));

    // And the loop reads flat about its mean: the numbers behind "loop a..b dB".
    const auto flatness = rta::ir::bandFlatness(loop, sweep.validBandLowHz(), sweep.validBandHighHz());
    CHECK(outcome.loopMinDb == flatness.minDb);
    CHECK(outcome.loopMaxDb == flatness.maxDb);
}

TEST_CASE("a measurement that reaches full scale is flagged but not refused", "[sweep_ir_pipeline]") {
    const auto request = shortRequest();
    auto capture = fx::delayCapture(request.sweep, kPreRoll,
                                    rta::measure::requiredTailSamples(request), 37);
    CHECK_FALSE(analyseSweepCapture(capture, request).measurementClipped);
    // 12 dB of gain into a hard clip at full scale: a clipped ESS, whose
    // harmonics fall at NEGATIVE time and leave the linear IR in place
    // (record sec. 3, owner decision 9).
    for (auto& x : capture.measurement) x = std::clamp(4.0f * x, -1.0f, 1.0f);
    const auto outcome = analyseSweepCapture(capture, request);
    CHECK(outcome.status == SweepIrStatus::Ok);
    CHECK(outcome.measurementClipped);
    CHECK(outcome.delaySamples == 37);
}
