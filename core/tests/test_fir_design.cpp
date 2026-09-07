// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TDD sequence for FirDesign -- docs/plans/2026-09-07-L7-fir-impl-plan.md
// task F1 (T1-T4), F2 (T5-T9), F3 (T10-T13). Written from the closed-form
// acceptances in docs/dsp/2026-09-06-l7-fir-export.md Sec.7.
//
// Tolerance shapes (plan "Global constraints"):
//   Shape A (FFT-derived): tol_k = 1e-6*|X_k| + c_M*peak, c_M = 2e-7 for
//     M <= 2^20 as the starting point -- the residual is printed beside the
//     tolerance and c_M is tightened if a fixture reads an order of
//     magnitude under it (memory/a-fixture-can-be-too-well-behaved-to-fail.md).
//   Trivial-target deltas: absolute 1e-5.
//   Grid interpolation (double, no FFT): 1e-12.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/FirDesign.h"
#include "rta/dsp/RealFft.h"

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

using namespace rta::dsp;

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kCm = 2e-7;   // Shape A peak coefficient, M <= 2^20 (starting point)

double peakAbs(std::span<const float> xs) {
    double peak = 0.0;
    for (float x : xs) peak = std::max(peak, std::abs(static_cast<double>(x)));
    return peak;
}

double shapeATolerance(double expectedAtK, double peakOverArray) {
    return 1e-6 * std::abs(expectedAtK) + kCm * peakOverArray;
}

FirTarget flatTarget(double gainDb) {
    return FirTarget{ std::vector<double>{ 20.0, 20000.0 }, std::vector<double>{ gainDb, gainDb } };
}

}  // namespace

TEST_CASE("A flat 0 dB target, linear phase, is a centred delta", "[fir_design]") {
    // T1 (first, plan F1). A flat magnitude/zero-phase target's inverse
    // transform IS a delta at n=0 before the circular shift; after centring
    // at (N-1)/2 and windowing (a window's own value at its own centre-tap
    // position is folded into the tap value only through the window shape,
    // but a delta convolved with nothing stays a delta -- windowing a delta
    // just scales it by the window's centre coefficient, which is 1.0 for
    // every periodic window this project ships at n=(N-1)/2 for odd N,
    // record Sec.7 item 4) reproduces a centred delta of amplitude 1.
    constexpr std::size_t n = 1023;
    const auto result = designFir(flatTarget(0.0), 48000.0, n, FirPhase::Linear);

    REQUIRE(result.taps.size() == n);
    CHECK(result.groupDelaySamples == (n - 1) / 2);

    const std::size_t centre = (n - 1) / 2;
    for (std::size_t i = 0; i < n; ++i) {
        const double expected = (i == centre) ? 1.0 : 0.0;
        CAPTURE(i, result.taps[i], expected);
        CHECK(std::abs(static_cast<double>(result.taps[i]) - expected) <= 1e-5);
    }

    CHECK(std::abs(result.peakGainDb) <= 1e-4);
    CHECK(std::abs(result.coefficientPeak - 1.0) <= 1e-5);
}

TEST_CASE("A flat +6.0206 dB target, linear phase, scales the centre tap to 2.0", "[fir_design]") {
    // T2. +6.0206 dB is exactly 20*log10(2) to the golden's precision --
    // doubling amplitude, closed form.
    constexpr std::size_t n = 1023;
    constexpr double gainDb = 6.0206;
    const auto result = designFir(flatTarget(gainDb), 48000.0, n, FirPhase::Linear);

    const std::size_t centre = (n - 1) / 2;
    CHECK(std::abs(static_cast<double>(result.taps[centre]) - 2.0) <= 1e-5);
    CHECK(std::abs(result.peakGainDb - gainDb) <= 1e-4);
    CHECK(std::abs(result.coefficientPeak - 2.0) <= 1e-5);
}

TEST_CASE("The sampled target survives its own forward/inverse round trip", "[fir_design]") {
    // T3. Proves the sample/invert step in isolation (record Sec.7 item 1):
    // an M-point IDFT of the sampled target, forward-FFT'd again before
    // windowing, must reproduce the sampled target bin for bin. This does
    // NOT depend on designFir's window/shift/truncate steps at all -- it
    // exercises RealFft the same way FirDesign's frequency-sampling core
    // does, on a target built the same way (flat, so the expected bins are
    // known in closed form: 1+0j at every bin).
    constexpr std::size_t m = 8192;   // a plausible M for a few-hundred-tap filter
    RealFft fft(m);
    const std::size_t bins = fft.numBins();

    std::vector<std::complex<float>> spectrum(bins, std::complex<float>(1.0f, 0.0f));
    std::vector<float> timeDomain(m);
    fft.inverse(spectrum, timeDomain);

    std::vector<std::complex<float>> roundTrip(bins);
    fft.forward(timeDomain, roundTrip);

    const double peak = 1.0;
    for (std::size_t k = 0; k < bins; ++k) {
        const double expectedRe = 1.0, expectedIm = 0.0;
        const double residualRe = std::abs(static_cast<double>(roundTrip[k].real()) - expectedRe);
        const double residualIm = std::abs(static_cast<double>(roundTrip[k].imag()) - expectedIm);
        const double tol = shapeATolerance(1.0, peak);
        CAPTURE(k, residualRe, residualIm, tol);
        CHECK(residualRe <= tol);
        CHECK(residualIm <= tol);
    }
}

namespace {

/// H(e^{jw_k}) for tap count n, evaluated directly from the taps -- an
/// independent computation from whatever internal FFT designFir used, so
/// this is a real second measurement, not a re-read of the same numbers.
std::complex<double> evaluateResponse(std::span<const float> taps, double w) {
    std::complex<double> acc{ 0.0, 0.0 };
    for (std::size_t n = 0; n < taps.size(); ++n) {
        acc += static_cast<double>(taps[n]) * std::polar(1.0, -w * static_cast<double>(n));
    }
    return acc;
}

}  // namespace

TEST_CASE("Linear-phase symmetry is bitwise, and the phase is exactly linear", "[fir_design]") {
    // T4. taps[n] == taps[N-1-n] by CONSTRUCTION (not to rounding): FirDesign
    // builds only the first half and mirrors it explicitly. Given that
    // symmetry, Im(H(e^{jw})*e^{+jw(N-1)/2}) == 0 at every bin is pure
    // algebra (Type I/II symmetry), independent of the tap values -- so this
    // is Shape A on the RESIDUAL of that algebraic identity, not a
    // reproduction of a chosen answer.
    const FirTarget target{ std::vector<double>{ 100.0, 1000.0, 10000.0 },
                             std::vector<double>{ -3.0, 6.0, -2.0 } };

    for (const std::size_t n : { std::size_t{ 1023 }, std::size_t{ 1024 } }) {
        const auto result = designFir(target, 48000.0, n, FirPhase::Linear);
        REQUIRE(result.taps.size() == n);

        for (std::size_t i = 0; i < n; ++i) {
            CAPTURE(n, i);
            CHECK(result.taps[i] == result.taps[n - 1 - i]);
        }

        if (n % 2 == 1) {
            CHECK(result.groupDelaySamples == (n - 1) / 2);
        } else {
            CHECK(result.groupDelaySamples == n / 2);
        }

        const double groupDelay = static_cast<double>(n - 1) / 2.0;
        const double peak = peakAbs(result.taps);
        constexpr std::size_t kBinCount = 64;
        for (std::size_t b = 1; b < kBinCount; ++b) {
            const double w = kPi * static_cast<double>(b) / static_cast<double>(kBinCount);
            const std::complex<double> h = evaluateResponse(result.taps, w);
            const std::complex<double> derotated = h * std::polar(1.0, w * groupDelay);
            const double residual = std::abs(derotated.imag());
            const double tol = shapeATolerance(0.0, peak * static_cast<double>(n));
            CAPTURE(n, b, residual, tol);
            CHECK(residual <= tol);
        }
    }
}
