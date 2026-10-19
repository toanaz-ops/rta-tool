// SPDX-License-Identifier: AGPL-3.0-or-later
//
// SWEEP lane task T3 row (b) (docs/plans/2026-09-29-sweep-ir-lane-plan.md,
// "Device-free acceptance tolerance"). JUCE-free; runs the SHIPPED 10 s
// configuration at 48 kHz, so it is the slowest case in the lane.
//
// ## What is asserted, and why the bound has this shape
//
// Fixture: reference = [P zeros, sweep, tail zeros], P = 12345; measurement =
// delta_D (x) g (x) reference with D = 37 and g = Butterworth band-pass 300-3000
// Hz, 2 sections. Its 300 Hz skirt is the record's case with teeth (record
// :900-905): a window that slices the analysis pulse down the middle reads
// tens of dB wrong there while a pure-delay test stays exact.
//
// Deconvolution is linear, so with r~ the normalised deconvolution of the
// reference and h = delta_D (x) g, the pipeline's measurement bins are
//     S_m = DFT_N[ w . (h (x) r~) ]           (w = rectangular window)
// and the loop-through-itself bins are S_r = DFT_N[ w . r~ ]. The target is
// G.S_r with G(k) = cascadeResponse(g, w_k).exp(-i w_k D). The difference
// S_m - G.S_r can come from only three places, each bounded using
// |DFT_k(x)| <= ||x||_1:
//
//   T1  r~ OUTSIDE the window leaking INTO it through h:
//         ||w . (g_D (x) ((1-w) r~))||_1.
//   T2  the window's content smeared PAST its end by h, which a circular
//       transform folds back (G.S_r is exactly the circular product):
//         ||(1-w) . (g_D (x) (w r~))||_1.
//   T3  h's tail beyond M samples, which g_D drops. It enters S_m (windowed
//       r~ through the tail, <= ||r~||_1 ||g_tail||_1) AND G.S_r (G_tail . S_r,
//       |S_r| <= ||r~||_1), so it counts TWICE:  2 ||r~||_1 ||g_tail||_1.
//       The plan writes this term once; it is a factor of two short, and
//       ||g_tail||_1 is ~1e-12 here so nothing turns on it -- but the bound
//       is stated as derived, not as inherited.
//
//   B_win = T1 + T2 + T3.
//
// Float rounding: S_m comes from three 2^21-point transforms inside
// `deconvolve` and one N-point transform in `analyseSpectrum`; S_r the same. The
// two-term shape of memory/float32-fft-precision.md is
//     B_float(k) = eps32 . sum_s(log2 N_s + 1) . (|G.S_r(k)| + max_j |S_m(j)|)
// with eps32 = numeric_limits<float>::epsilon(). One more stage is added to the
// sum: the filtered measurement is rounded to float before it enters the
// pipeline (the plan's four transforms do not count that step), worth
// (log2 1 + 1) = 1.
//
// The window w is the INTENDED one, computed here from the plan's sentence
// (decision 4) -- [refPeak - leadIn, +N) with leadIn = round(2 fs / f_lo) --
// never read back from the pipeline. Were it read back, a window that moved
// would move its own bound and the test could not go red.
#include <catch2/catch_test_macros.hpp>

#include "SweepIrFixture.h"
#include "measure/SweepIrPipeline.h"
#include "rta/dsp/Biquad.h"
#include "rta/dsp/BiquadResponse.h"
#include "rta/dsp/ButterworthDesign.h"
#include "rta/ir/Deconvolver.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <limits>
#include <vector>

namespace fx = sweep_fixture;
using rta::measure::SweepIrStatus;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 48000.0;
constexpr std::size_t kPreRoll = 12345;  // the plan's P
constexpr std::size_t kDelay = 37;       // the plan's D

std::size_t nextPow2(std::size_t n) {
    std::size_t size = 4;
    while (size < n) size <<= 1;
    return size;
}

int log2Of(std::size_t powerOfTwo) {
    int n = 0;
    while ((std::size_t{1} << n) < powerOfTwo) ++n;
    return n;
}

}  // namespace

TEST_CASE("the shipped sweep's bins match G.S_r within the derived B_win + B_float",
          "[sweep_ir_tolerance]") {
    rta::measure::SweepIrRequest request;
    request.sweep = rta::measure::shippedSweepConfig(kFs);
    request.tailSeconds = 3.0;
    const rta::gen::Sweep sweep(request.sweep);
    const std::size_t sweepSamples = sweep.lengthSamples();
    const double fLo = sweep.validBandLowHz();
    const double fHi = sweep.validBandHighHz();
    const std::size_t total = kPreRoll + sweepSamples + rta::measure::requiredTailSamples(request);

    // --- the fixture -------------------------------------------------------
    const auto signal = fx::sweepSignal(request.sweep);
    rta::measure::SweepCapture capture;
    capture.reference = fx::placed(signal, kPreRoll, total);
    const auto g = rta::dsp::ButterworthDesign::bandPass(300.0, 3000.0, kFs, 2);
    rta::dsp::BiquadCascade cascade(g.sections);
    std::vector<float> filtered(total);
    for (std::size_t i = 0; i < total; ++i) {
        filtered[i] = static_cast<float>(cascade.processSample(static_cast<double>(capture.reference[i])));
    }
    capture.measurement = fx::delayed(filtered, kDelay);

    // --- the pipeline, twice -----------------------------------------------
    const auto measured = rta::measure::analyseSweepCapture(capture, request);
    rta::measure::SweepCapture loopOnly{capture.reference, capture.reference};
    const auto loop = rta::measure::analyseSweepCapture(loopOnly, request);
    INFO(measured.message << " / " << loop.message);
    REQUIRE(measured.status == SweepIrStatus::Ok);
    REQUIRE(loop.status == SweepIrStatus::Ok);
    REQUIRE(measured.fftSize == loop.fftSize);
    const std::size_t fftN = measured.fftSize;
    REQUIRE(fftN == 32768);

    // --- r~, independently of the pipeline's outcome ------------------------
    const auto inverse = sweep.buildInverseFilter();
    rta::ir::DeconvolverConfig deconvConfig;
    deconvConfig.sampleRate = kFs;
    const auto raw = rta::ir::deconvolve(capture.reference, inverse, deconvConfig);
    deconvConfig.normalisationGain = rta::ir::inBandNormalisation(raw, fLo, fHi);
    const auto rTilde = rta::ir::deconvolve(capture.reference, inverse, deconvConfig).samples;
    std::size_t refPeak = 0;
    for (std::size_t i = 1; i < rTilde.size(); ++i) {
        if (std::fabs(rTilde[i]) > std::fabs(rTilde[refPeak])) refPeak = i;
    }
    REQUIRE(refPeak == (sweepSamples - 1) + kPreRoll);

    const auto leadIn = static_cast<std::size_t>(std::llround(2.0 * kFs / fLo));
    REQUIRE(nextPow2(static_cast<std::size_t>(std::ceil(0.5 * kFs)) + leadIn) == fftN);
    const std::size_t start = refPeak - leadIn;  // intended window [start, end)
    const std::size_t end = start + fftN;

    // --- g_D, truncated at M, and its tail ----------------------------------
    // M: where rho^M / (1 - rho) falls under 1e-12 of the unit-scale pulse.
    const double rho = cascade.maxPoleRadius();
    const auto taps = static_cast<std::size_t>(std::ceil(std::log(1e-12 * (1.0 - rho)) / std::log(rho)));
    const std::size_t longTaps = 4 * taps;
    rta::dsp::BiquadCascade impulse(g.sections);
    std::vector<double> gFull(longTaps);
    for (std::size_t n = 0; n < longTaps; ++n) gFull[n] = impulse.processSample(n == 0 ? 1.0 : 0.0);
    double tailL1 = 0.0;
    for (std::size_t n = taps; n < longTaps; ++n) tailL1 += std::fabs(gFull[n]);
    const double geometric = std::pow(rho, static_cast<double>(taps)) / (1.0 - rho);
    std::printf("[sweep_ir_tolerance] rho=%.6f M=%zu  ||g_tail||_1=%.3e  rho^M/(1-rho)=%.3e\n", rho,
                taps, tailL1, geometric);
    // The plan asks for ||g_tail||_1 to be checked against the geometric bound.
    CHECK(tailL1 <= geometric);

    const std::size_t support = kDelay + taps;  // g_D has support [D, D + M)
    std::vector<double> gD(support, 0.0);
    for (std::size_t n = 0; n < taps; ++n) gD[kDelay + n] = gFull[n];

    // --- B_win ---------------------------------------------------------------
    double t1 = 0.0;  // r~ left of the window, through g_D, landing inside it
    for (std::size_t n = start; n < start + support; ++n) {
        double acc = 0.0;
        for (std::size_t j = n - start + 1; j < support; ++j) acc += gD[j] * rTilde[n - j];
        t1 += std::fabs(acc);
    }
    double t2 = 0.0;  // the window's content, through g_D, landing past its end
    for (std::size_t n = end; n < end + support; ++n) {
        double acc = 0.0;
        for (std::size_t j = n - end + 1; j < support; ++j) acc += gD[j] * rTilde[n - j];
        t2 += std::fabs(acc);
    }
    double rL1 = 0.0;
    for (const float x : rTilde) rL1 += std::fabs(static_cast<double>(x));
    const double bWin = t1 + t2 + 2.0 * rL1 * tailL1;

    // --- B_float's constant ----------------------------------------------------
    const std::size_t deconvN = nextPow2(total + sweepSamples - 1);
    const int stages = 3 * (log2Of(deconvN) + 1) + (log2Of(fftN) + 1) + 1;
    const double epsStages = static_cast<double>(std::numeric_limits<float>::epsilon()) * stages;
    double maxSm = 0.0;
    for (const auto& bin : measured.bins) maxSm = std::max(maxSm, static_cast<double>(std::abs(bin)));

    // --- the assertion ----------------------------------------------------------
    const double binHz = kFs / static_cast<double>(fftN);
    const auto kLo = static_cast<std::size_t>(std::ceil(fLo / binHz));
    const auto kHi = static_cast<std::size_t>(std::floor(fHi / binHz));
    double worstRatio = 0.0, worstRatioHz = 0.0;
    double worstBoundDb = 0.0, worstBoundDbHz = 0.0, worstPassbandDb = 0.0;
    std::size_t violations = 0;
    for (std::size_t k = kLo; k <= kHi; ++k) {
        const double omega = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(fftN);
        const std::complex<double> gain =
            rta::dsp::cascadeResponse(g.sections, omega) *
            std::exp(std::complex<double>(0.0, -omega * static_cast<double>(kDelay)));
        const std::complex<double> target =
            gain * std::complex<double>(loop.bins[k].real(), loop.bins[k].imag());
        const std::complex<double> actual(measured.bins[k].real(), measured.bins[k].imag());
        const double residual = std::abs(actual - target);
        const double targetMag = std::abs(target);
        const double bound = bWin + epsStages * (targetMag + maxSm);
        const double ratio = residual / bound;
        if (residual > bound) ++violations;
        const double hz = static_cast<double>(k) * binHz;
        if (ratio > worstRatio) { worstRatio = ratio; worstRatioHz = hz; }
        const double boundDb = 20.0 * std::log10(1.0 + bound / targetMag);
        if (boundDb > worstBoundDb) { worstBoundDb = boundDb; worstBoundDbHz = hz; }
        if (hz >= 300.0 && hz <= 3000.0) worstPassbandDb = std::max(worstPassbandDb, boundDb);
    }
    std::printf("[sweep_ir_tolerance] B_win=%.3e (T1=%.3e T2=%.3e) ||r~||_1=%.3f stages=%d "
                "eps*stages=%.3e max|S_m|=%.3f\n",
                bWin, t1, t2, rL1, stages, epsStages, maxSm);
    std::printf("[sweep_ir_tolerance] bins %zu..%zu (%.0f..%.0f Hz): worst residual/bound = %.4f at "
                "%.0f Hz\n",
                kLo, kHi, static_cast<double>(kLo) * binHz, static_cast<double>(kHi) * binHz,
                worstRatio, worstRatioHz);
    std::printf("[sweep_ir_tolerance] worst bound in dB = %.4f dB at %.0f Hz; worst within the "
                "300-3000 Hz passband = %.5f dB\n",
                worstBoundDb, worstBoundDbHz, worstPassbandDb);
    CHECK(violations == 0);
    CHECK(worstRatio <= 1.0);
}

// ---------------------------------------------------------------------------
// ABSOLUTE phase against the loop arrival.
//
// The test above compares the pipeline with itself, so a window that starts a
// few samples off cancels (both channels share the error). This one anchors the
// phase to a closed form: t = 0 is the loop peak, so a measurement delayed by D
// has phase -w_k D, w_k = 2 pi k / N, at every bin where the sweep spoke.
//
// Derived bound. Let x be the deconvolved pulse, windowed, and c the sample the
// pulse is centred on (the peak). S(k) = sum_n x[n] e^{-i w (n - c)}, so
//     Im S = -sum_{m>=1} (x[c+m] - x[c-m]) sin(w m),
//     |Im S| <= A := sum_{m>=1} |x[c+m] - x[c-m]|,
// where x is zero outside the window (the window cuts one side first, and that
// unmatched part is inside A). With Re S ~ |S| the phase error is at most
//     asin(min(1, (A + B_float(k)) / |S(k)|)),
// B_float as in the test above (transform-rounding shape, no measurement
// rounding here: the delayed measurement is an exact copy). A is computed from
// an independent deconvolution and the INTENDED window, never from the pipeline.
TEST_CASE("the phase is -w*D against the loop arrival, absolutely, for D = 0 and 37",
          "[sweep_ir_tolerance]") {
    rta::measure::SweepIrRequest request;
    request.sweep = rta::measure::shippedSweepConfig(kFs);
    request.sweep.durationSec = 2.0;
    request.tailSeconds = 0.6;
    const rta::gen::Sweep sweep(request.sweep);
    const double fLo = sweep.validBandLowHz();
    const double fHi = sweep.validBandHighHz();
    const std::size_t total = kPreRoll + sweep.lengthSamples() + rta::measure::requiredTailSamples(request);
    const auto reference = fx::placed(fx::sweepSignal(request.sweep), kPreRoll, total);

    const auto inverse = sweep.buildInverseFilter();
    rta::ir::DeconvolverConfig deconvConfig;
    deconvConfig.sampleRate = kFs;
    const auto raw = rta::ir::deconvolve(reference, inverse, deconvConfig);
    deconvConfig.normalisationGain = rta::ir::inBandNormalisation(raw, fLo, fHi);
    const auto rTilde = rta::ir::deconvolve(reference, inverse, deconvConfig).samples;
    std::size_t refPeak = 0;
    for (std::size_t i = 1; i < rTilde.size(); ++i) {
        if (std::fabs(rTilde[i]) > std::fabs(rTilde[refPeak])) refPeak = i;
    }
    const auto leadIn = static_cast<std::size_t>(std::llround(2.0 * kFs / fLo));
    const std::size_t fftN = nextPow2(static_cast<std::size_t>(std::ceil(0.5 * kFs)) + leadIn);
    const std::size_t start = refPeak - leadIn;  // intended window [start, start + N)

    const std::size_t deconvN = nextPow2(total + sweep.lengthSamples() - 1);
    const double epsStages = static_cast<double>(std::numeric_limits<float>::epsilon()) *
                             (3 * (log2Of(deconvN) + 1) + (log2Of(fftN) + 1));
    const double binHz = kFs / static_cast<double>(fftN);
    double worstRad = 0.0;
    double worstBound = 0.0;
    for (const std::size_t delay : {std::size_t{0}, kDelay}) {
        rta::measure::SweepCapture capture{reference, fx::delayed(reference, delay)};
        const auto outcome = rta::measure::analyseSweepCapture(capture, request);
        INFO(outcome.message);
        REQUIRE(outcome.status == SweepIrStatus::Ok);
        REQUIRE(outcome.fftSize == fftN);

        // x = r~ shifted by `delay`, cut to the intended window; centre = refPeak.
        auto at = [&](long n) {
            const long src = n - static_cast<long>(delay);
            const bool inWindow = n >= static_cast<long>(start) && n < static_cast<long>(start + fftN);
            return (inWindow && src >= 0) ? static_cast<double>(rTilde[static_cast<std::size_t>(src)]) : 0.0;
        };
        const long centre = static_cast<long>(refPeak + delay);
        double asymmetry = 0.0;
        for (long m = 1; m < static_cast<long>(fftN); ++m) asymmetry += std::fabs(at(centre + m) - at(centre - m));
        double maxBin = 0.0;
        for (const auto& bin : outcome.bins) maxBin = std::max(maxBin, static_cast<double>(std::abs(bin)));

        const auto kLo = static_cast<std::size_t>(std::ceil(300.0 / binHz));
        const auto kHi = static_cast<std::size_t>(std::floor(10000.0 / binHz));
        for (std::size_t k = kLo; k <= kHi; ++k) {
            const double omega = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(fftN);
            double diff = std::fmod(static_cast<double>(std::arg(outcome.bins[k])) +
                                        omega * static_cast<double>(delay),
                                    2.0 * kPi);
            if (diff > kPi) diff -= 2.0 * kPi;
            if (diff < -kPi) diff += 2.0 * kPi;
            const double mag = static_cast<double>(std::abs(outcome.bins[k]));
            const double bound = std::asin(std::min(1.0, (asymmetry + epsStages * (mag + maxBin)) / mag));
            worstRad = std::max(worstRad, std::fabs(diff));
            worstBound = std::max(worstBound, bound);
            INFO("D=" << delay << " k=" << k << " hz=" << static_cast<double>(k) * binHz
                      << " residual rad=" << diff << " bound=" << bound);
            REQUIRE(std::fabs(diff) <= bound);
        }
    }
    std::printf("[sweep_ir_tolerance] absolute phase 300-10k Hz: worst |err| = %.3e rad, worst "
                "bound = %.3e rad\n", worstRad, worstBound);
}
