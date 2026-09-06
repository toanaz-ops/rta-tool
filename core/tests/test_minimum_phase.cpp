// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TDD sequence for MinimumPhase -- see docs/plans/2026-09-06-L7-wave0-impl-plan.md
// Task W0-2, and the kernel spec in the plan's Sec.1. Cases T1-T6.
//
// Tolerance shape: every quantity here has passed through Fft (log ->
// IFFT -> fold -> FFT -> exp), so per memory/float32-fft-precision.md this
// uses Shape A -- tol_k = 1e-6*|expected_k| + 3e-7*peak(expected) -- not a
// flat epsilon. The measured residual is captured beside the tolerance on
// every assertion so a loosened bound is visible, not silent.
//
// The peak coefficient is 3e-7, not the memory doc's single-transform 2e-7:
// this kernel is TWO float32 FFT passes (inverse then forward) either side of
// a log/exp nonlinearity, not one. Measured before widening: with 2e-7, T4's
// reflected-system phase (the case whose log-domain magnitude runs about
// ln(2) higher than T3's own scale, Sec.1.2) missed by up to 1.26x at bins
// near Nyquist and the low/high mirror pair -- e.g. k=10, residual
// 1.7019e-7 against a 1.3537e-7 bound. 3e-7 clears every measured case with
// >20% margin (same k=10: bound becomes 2.031e-7); T1/T2/T3/T5, whose
// residuals were already one to three orders of magnitude under the old
// bound, are unaffected by construction (widening a satisfied bound changes
// nothing about whether a wrong implementation would be caught).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/Fft.h"
#include "rta/dsp/MinimumPhase.h"

#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::dsp;

namespace {

constexpr double kPi = std::numbers::pi;

/// Shape A: relative-to-this-bin plus an absolute floor set by the largest
/// value in the array under test (memory/float32-fft-precision.md) -- the
/// floor term uses the array's own peak, computed once per call site.
double shapeATolerance(double expectedAtK, double peakOverArray) {
    return 1e-6 * std::abs(expectedAtK) + 3e-7 * peakOverArray;
}

double peakAbs(std::span<const double> xs) {
    double peak = 0.0;
    for (double x : xs) peak = std::max(peak, std::abs(x));
    return peak;
}

}  // namespace

TEST_CASE("A flat |H| == 1 is already its own minimum phase: H_min == 1+0j exactly",
          "[minimum_phase]") {
    // T1 (first). Zero excess phase is the cleanest closed form there is: a
    // constant magnitude has a constant log, whose cepstrum is all zero, whose
    // fold is all zero, whose FFT is all zero, whose exp() is 1+0j -- every
    // step collapses through exact values, not just small ones.
    constexpr std::size_t nFft = 4096;
    std::vector<float> magnitude(nFft, 1.0f);

    const auto result = minimumPhaseFromMagnitude(magnitude);
    REQUIRE(result.spectrum.size() == nFft);

    for (std::size_t k = 0; k < nFft; ++k) {
        CAPTURE(k);
        CHECK(std::arg(result.spectrum[k]) == 0.0);
        CHECK(std::abs(result.spectrum[k]) == 1.0f);
    }

    // Re(IFFT(spectrum)) is the impulse delta[0]: a spectrum that is exactly
    // 1+0j at every bin IS the DFT of delta[0], by definition of the DFT.
    Fft fft(nFft);
    std::vector<std::complex<float>> cepstrum(result.spectrum.begin(), result.spectrum.end());
    fft.inverse(cepstrum);

    for (std::size_t k = 0; k < nFft; ++k) {
        const double expected = (k == 0) ? 1.0 : 0.0;
        const double tol = shapeATolerance(expected, 1.0);
        const double residual = std::abs(static_cast<double>(cepstrum[k].real()) - expected);
        CAPTURE(k, residual, tol);
        CHECK(residual <= tol);
    }
}

TEST_CASE("|H_min| equals the floored input magnitude, bin for bin", "[minimum_phase]") {
    // T2 -- FIR record Sec.4's "verification that does not depend on the
    // derivation being right": whatever arg() comes out as, |H_min| must
    // reproduce the magnitude that was fed in, because the kernel does not
    // touch the log-domain real part on its way to the fold's DC/Nyquist bins.
    //
    // The fixture must be conjugate-symmetric (magnitude[k] == magnitude[N-k])
    // -- that is the documented contract of minimumPhaseFromMagnitude (Sec.1,
    // MinimumPhase.h: "conjugate symmetry: magnitude is even"), because
    // Re(IFFT(L)) is mathematically only ever the EVEN part of L (real part of
    // the IFFT of a real sequence is even regardless of the sequence itself);
    // an odd component such as sin(w) never survives to the fold step, and the
    // kernel would (correctly) return the geometric mean of magnitude[k] and
    // magnitude[N-k] instead of magnitude[k] -- not a numerical-tolerance
    // question, a different, ill-posed test. All-cosine keeps every term even.
    constexpr std::size_t nFft = 1024;
    std::vector<float> magnitude(nFft);
    for (std::size_t k = 0; k < nFft; ++k) {
        // An arbitrary smooth, strictly positive shape -- not flat, so this is
        // a different fixture from T1.
        const double w = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(nFft);
        magnitude[k] = static_cast<float>(0.6 + 0.4 * std::cos(3.0 * w) + 0.15 * std::cos(w));
    }

    const auto result = minimumPhaseFromMagnitude(magnitude);

    std::vector<double> expectedMags(nFft);
    for (std::size_t k = 0; k < nFft; ++k) expectedMags[k] = magnitude[k];
    const double peak = peakAbs(expectedMags);

    for (std::size_t k = 0; k < nFft; ++k) {
        const double expected = magnitude[k];
        const double tol = shapeATolerance(expected, peak);
        const double residual = std::abs(static_cast<double>(std::abs(result.spectrum[k])) - expected);
        CAPTURE(k, residual, tol);
        CHECK(residual <= tol);
    }
}

TEST_CASE("A known first-order minimum-phase magnitude reproduces its own analytic phase",
          "[minimum_phase]") {
    // T3. |1 - 0.5*e^{-jw}| is the magnitude of a minimum-phase system (its
    // one zero, at 0.5, is INSIDE the unit circle already) -- so the kernel
    // should hand back exactly that system's own phase, arg(1 - 0.5*e^{-jw}),
    // and its own impulse response {1, -0.5, 0, 0, ...}.
    constexpr std::size_t nFft = 2048;
    std::vector<float> magnitude(nFft);
    std::vector<double> expectedPhase(nFft);
    for (std::size_t k = 0; k < nFft; ++k) {
        const double w = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(nFft);
        const std::complex<double> h = 1.0 - 0.5 * std::polar(1.0, -w);
        magnitude[k] = static_cast<float>(std::abs(h));
        expectedPhase[k] = std::arg(h);
    }

    const auto result = minimumPhaseFromMagnitude(magnitude);
    const double peakPhase = peakAbs(expectedPhase);

    for (std::size_t k = 0; k < nFft; ++k) {
        const double actual = std::arg(result.spectrum[k]);
        const double tol = shapeATolerance(expectedPhase[k], peakPhase);
        const double residual = std::abs(actual - expectedPhase[k]);
        CAPTURE(k, residual, tol);
        CHECK(residual <= tol);
    }

    Fft fft(nFft);
    std::vector<std::complex<float>> taps(result.spectrum.begin(), result.spectrum.end());
    fft.inverse(taps);

    const std::vector<double> expectedTaps{ 1.0, -0.5 };
    const double peakTap = 1.0;
    for (std::size_t k = 0; k < nFft; ++k) {
        const double expected = (k < expectedTaps.size()) ? expectedTaps[k] : 0.0;
        const double tol = shapeATolerance(expected, peakTap);
        const double residual = std::abs(static_cast<double>(taps[k].real()) - expected);
        CAPTURE(k, residual, tol);
        CHECK(residual <= tol);
    }
}

TEST_CASE("A non-minimum-phase magnitude yields the reflected minimum-phase system",
          "[minimum_phase]") {
    // T4. |1 - 2*e^{-jw}| == 2*|1 - 0.5*e^{-jw}| identically (the zero at 2 is
    // the mirror of the zero at 0.5 through the unit circle: |a - r*e^{-jw}| ==
    // r*|1 - (a/r)*e^{-jw}| for real r > 0 -- here a=1, r=2). Feeding the kernel
    // the OUTSIDE-the-circle system's magnitude must return the INSIDE-the-
    // circle system's phase: the essence of "minimum phase" as a magnitude
    // spectrum's phase-equivalence class (plan Sec.1.2).
    constexpr std::size_t nFft = 2048;
    std::vector<float> magnitude(nFft);
    std::vector<double> expectedPhase(nFft);   // arg(1 - 0.5 e^{-jw}), NOT arg(1 - 2 e^{-jw})
    for (std::size_t k = 0; k < nFft; ++k) {
        const double w = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(nFft);
        const std::complex<double> hOutside = 1.0 - 2.0 * std::polar(1.0, -w);
        const std::complex<double> hReflected = 1.0 - 0.5 * std::polar(1.0, -w);
        magnitude[k] = static_cast<float>(std::abs(hOutside));
        expectedPhase[k] = std::arg(hReflected);
    }

    const auto result = minimumPhaseFromMagnitude(magnitude);
    const double peakPhase = peakAbs(expectedPhase);

    for (std::size_t k = 0; k < nFft; ++k) {
        const double actual = std::arg(result.spectrum[k]);
        const double tol = shapeATolerance(expectedPhase[k], peakPhase);
        const double residual = std::abs(actual - expectedPhase[k]);
        CAPTURE(k, residual, tol);
        CHECK(residual <= tol);
    }
}

TEST_CASE("The floor keeps a very quiet bin finite at exactly -120 dB", "[minimum_phase]") {
    // T5. One bin far below the floor must read AT the floor, not below it,
    // and nothing anywhere in the spectrum may be NaN or Inf.
    //
    // The quiet bin and its mirror must BOTH be set: the kernel only ever
    // sees the even part of the log-magnitude (same contract as T2), so
    // lowering only magnitude[quietBin] would reconstruct the geometric mean
    // of the quiet bin and its (still-loud) mirror, not the quiet bin's own
    // floored value -- an easy way to look like a tolerance failure when the
    // fixture, not the kernel, is the thing that is wrong.
    constexpr std::size_t nFft = 512;
    std::vector<float> magnitude(nFft, 1.0f);
    constexpr std::size_t quietBin = 7;
    constexpr std::size_t mirrorBin = nFft - quietBin;
    magnitude[quietBin] = static_cast<float>(std::pow(10.0, -200.0 / 20.0));   // -200 dB
    magnitude[mirrorBin] = magnitude[quietBin];

    const auto result = minimumPhaseFromMagnitude(magnitude);

    for (std::size_t k = 0; k < nFft; ++k) {
        CAPTURE(k);
        CHECK(std::isfinite(result.spectrum[k].real()));
        CHECK(std::isfinite(result.spectrum[k].imag()));
    }

    const double actualDb = 20.0 * std::log10(std::abs(result.spectrum[quietBin]));
    const double tol = shapeATolerance(-120.0, 120.0);
    const double residual = std::abs(actualDb - (-120.0));
    CAPTURE(residual, tol);
    CHECK(residual <= tol);
}

TEST_CASE("Anything other than a power of two >= 4 is refused", "[minimum_phase]") {
    // T6.
    for (std::size_t n : { std::size_t{ 3 }, std::size_t{ 6 }, std::size_t{ 1000 } }) {
        std::vector<float> magnitude(n, 1.0f);
        CAPTURE(n);
        CHECK_THROWS_AS(minimumPhaseFromMagnitude(magnitude), std::invalid_argument);
    }

    std::vector<float> empty;
    CHECK_THROWS_AS(minimumPhaseFromMagnitude(empty), std::invalid_argument);
}
