// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Even-N FIR design (defect fixed 2026-09-29). Before the fix, designFir()
// with an even tap count wrote the zero-phase centre sample h_zero[0] to BOTH
// taps N/2-1 and N/2, realising the target's delta component twice: a flat
// 0 dB target read |H(DC)| = 2.04 at N = 1024 and 4096, minimum phase
// included (it reuses the linear design). Every magnitude test in this
// directory used an odd N, where the construction is exact, so nothing saw it.
//
// Every expectation here is closed form (docs/dsp/2026-09-06-l7-fir-export.md,
// amendment 2026-09-29), none is "whatever the code printed".
//
// Closed form used in (a): the flat target's M-point half-grid is 1 at bins
// 0..M/2-1 and the Nyquist bin is forced to 0 by the half-sample shift (a
// type II filter has H(pi) = 0). The tap N/2+j is then the half-sample
// Dirichlet sum
//     g[j] = (1/M) * sum_{k=-(M/2-1)}^{M/2-1} e^{j 2 pi k (j+1/2) / M}
//          = (-1)^j * cot( pi (j+1/2) / M ) / M,
// which tends to the ideal half-sample sinc, sin(pi/2)/(pi/2) = 2/pi at j = 0.

#include <catch2/catch_test_macros.hpp>

#include "rta/dsp/FirDesign.h"

#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <vector>

using namespace rta::dsp;

namespace {

constexpr double kPi = std::numbers::pi;

FirTarget flatTarget() {
    return FirTarget{ std::vector<double>{ 20.0, 20000.0 }, std::vector<double>{ 0.0, 0.0 } };
}

/// Periodic Hann, the same closed form Window.cpp evaluates (in double).
double hann(std::size_t i, std::size_t n) {
    return 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(n));
}

std::size_t designGridSize(std::size_t taps) {
    std::size_t m = 1;
    while (m < 8 * taps) m <<= 1;
    return m;
}

double sumOf(const std::vector<float>& taps) {
    double s = 0.0;
    for (float t : taps) s += static_cast<double>(t);
    return s;
}

std::complex<double> responseAt(const std::vector<float>& taps, double hz, double fs) {
    std::complex<double> acc{ 0.0, 0.0 };
    const double w = 2.0 * kPi * hz / fs;
    for (std::size_t i = 0; i < taps.size(); ++i) {
        acc += static_cast<double>(taps[i]) * std::polar(1.0, -w * static_cast<double>(i));
    }
    return acc;
}

const char* phaseName(FirPhase p) { return p == FirPhase::Linear ? "linear" : "minimum"; }

/// Shared assertions for "flat 0 dB, even N" whatever route produced `result`.
void checkFlatEven(const FirResult& result, std::size_t n, FirPhase phase) {
    REQUIRE(result.taps.size() == n);

    // Sum of the taps IS H(e^{j0}); the target is 1. Bound: window leakage at
    // DC from the only spectral feature, the Nyquist null, is a Hann kernel
    // tail at n/2 bins (~1/(pi k^3) ~ 1e-7 for N >= 1024); the rest is float
    // FFT noise (~1e-6). 1e-3 is a stated margin over both; the defect read
    // 1.04 above it.
    const double dc = sumOf(result.taps);
    CAPTURE(n, phaseName(phase), dc);
    CHECK(std::abs(dc - 1.0) <= 1e-3);

    if (phase != FirPhase::Linear) return;

    const std::size_t m = result.designFftSize;
    REQUIRE(m == designGridSize(n));

    for (std::size_t i = 0; i < n; ++i) {
        CAPTURE(i);
        CHECK(result.taps[i] == result.taps[n - 1 - i]);   // bitwise
    }

    // The two centre taps are the SAME half-sample sample of the response,
    // so they are equal, and each is ~2/pi (never ~1, the defect's value).
    const double centre = static_cast<double>(result.taps[n / 2]);
    CHECK(result.taps[n / 2 - 1] == result.taps[n / 2]);
    CHECK(centre > 0.6);
    CHECK(centre < 0.7);

    // Every tap against the closed form. Tolerance: the largest tap is 2/pi
    // ~ 0.64, one float ulp of which is 6e-8; measured worst residual is
    // 1.4e-8 (N=1024) and 5.1e-8 (N=4096), i.e. under one ulp. 5e-7 is about
    // eight ulp of the peak tap -- room for a different libm or FFT rounding,
    // and still 7e5 times below the defect's 0.36.
    double worst = 0.0;
    for (std::size_t j = 0; j < n / 2; ++j) {
        const double x = kPi * (static_cast<double>(j) + 0.5) / static_cast<double>(m);
        const double g = ((j % 2 == 0) ? 1.0 : -1.0) / (std::tan(x) * static_cast<double>(m));
        const double expected = g * hann(n / 2 - 1 - j, n);
        worst = std::max(worst, std::abs(static_cast<double>(result.taps[n / 2 + j]) - expected));
    }
    CAPTURE(worst);
    CHECK(worst <= 5e-7);
}

}  // namespace

TEST_CASE("Even N, flat 0 dB target: DC gain is 1 and the delta is realised once",
          "[fir_design]") {
    for (const std::size_t n : { std::size_t{ 1024 }, std::size_t{ 4096 } }) {
        for (const FirPhase phase : { FirPhase::Linear, FirPhase::Minimum }) {
            const auto result = designFir(flatTarget(), 48000.0, n, phase);
            checkFlatEven(result, n, phase);
        }
    }
}

TEST_CASE("Even N through the per-bin overload (the route L7-EQ uses)", "[fir_design]") {
    constexpr std::size_t n = 4096;
    const std::size_t m = designGridSize(n);
    const std::vector<float> grid(m / 2 + 1, 1.0f);
    for (const FirPhase phase : { FirPhase::Linear, FirPhase::Minimum }) {
        const auto result = designFir(std::span<const float>(grid), 48000.0, n, phase);
        checkFlatEven(result, n, phase);
    }
}

TEST_CASE("Plateau target reads its closed-form gain at 1500 Hz, odd and even N",
          "[fir_design]") {
    // 20*log10(2) = 6.0206 dB -> exactly 2.0 in amplitude (the same
    // convention test_fir_design.cpp T2 uses). 1500 Hz is inside the flat
    // (500 Hz, 5 kHz) plateau, so the target there is 2.0 with no
    // interpolation involved.
    //
    // Tolerance: at N = 1024 the nearest breakpoint (500 Hz) is 1000 Hz =
    // 21.3 bins of fs/N away, where the Hann kernel envelope is ~1/(pi k^3)
    // ~ 3e-5 relative (and oscillates, so it cancels further against a kink);
    // float FFT noise adds ~1e-6. 1e-3 absolute is a stated safety margin
    // over both, not a fitted number. The defect read 3.26 here at N = 1024
    // and 4096 (both phases); the odd-N control never failed.
    const FirTarget target{ std::vector<double>{ 20.0, 200.0, 500.0, 5000.0, 10000.0, 20000.0 },
                            std::vector<double>{ 0.0, 0.0, 6.0206, 6.0206, 0.0, 0.0 } };
    const double expected = std::pow(10.0, 6.0206 / 20.0);
    constexpr double fs = 48000.0;

    for (const std::size_t n : { std::size_t{ 1023 }, std::size_t{ 1024 }, std::size_t{ 4096 } }) {
        for (const FirPhase phase : { FirPhase::Linear, FirPhase::Minimum }) {
            const auto result = designFir(target, fs, n, phase);
            const double mag = std::abs(responseAt(result.taps, 1500.0, fs));
            CAPTURE(n, phaseName(phase), mag, expected);
            CHECK(std::abs(mag - expected) <= 1e-3);
        }
    }
}
