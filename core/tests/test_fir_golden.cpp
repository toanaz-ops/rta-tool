// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Pinned against tools/gen_fir.py's numpy/scipy reimplementation -- lane L7,
// sub-lane L7-FIR, task F6 (docs/plans/2026-09-07-L7-fir-impl-plan.md).
// Frequency-sampling taps: numpy.fft.irfft, a second author of
// FirDesign.cpp's own algorithm (record docs/dsp/2026-09-06-l7-fir-export.md
// Sec.7 item 7). Minimum-phase taps (added task F2, once the cepstral
// oversampling factor is wired into designFir) are compared against
// scipy.signal.minimum_phase directly -- scipy as the second author.
//
// Both paths are FFTs, so Shape A applies to both (the log/exp pair in the
// minimum-phase path maps a relative magnitude error to the same relative
// error, per the record's own reasoning for reusing one tolerance shape).

#include "rta/dsp/FirDesign.h"
#include "support/Golden.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

using namespace rta::dsp;

namespace {

const std::vector<rta::test::GoldenCase>& firGolden() {
    static const auto cases = rta::test::loadGolden(std::string(RTA_GOLDEN_DIR) + "/fir.txt");
    return cases;
}

const rta::test::GoldenCase& findCase(const std::vector<rta::test::GoldenCase>& cases,
                                       const std::string& name) {
    for (const auto& c : cases) {
        if (c.name == name) return c;
    }
    throw std::runtime_error("golden case not found: " + name);
}

double peakAbs(const std::vector<float>& xs) {
    double peak = 0.0;
    for (float x : xs) peak = std::max(peak, std::abs(static_cast<double>(x)));
    return peak;
}

/// Shape A (plan "Global constraints"): tol_k = 1e-6*|X_k| + c_M*peak,
/// c_M = 2e-7 for M <= 2^20 as the starting point.
double shapeATolerance(double expectedAtK, double peak) {
    return 1e-6 * std::abs(expectedAtK) + 2e-7 * peak;
}

/// The SAME boost-and-cut breakpoint target tools/gen_fir.py's
/// TARGET_FREQS/TARGET_GAINS_DB build the golden from -- kept in sync by
/// hand (a data literal, not something either side can `#include`).
FirTarget boostAndCutTarget() {
    return FirTarget{
        std::vector<double>{ 20.0, 60.0, 200.0, 800.0, 3000.0, 8000.0, 20000.0 },
        std::vector<double>{ -2.0, 3.0, -6.0, 8.0, -4.0, 2.0, 0.0 },
    };
}

}  // namespace

TEST_CASE("C++ frequency-sampling taps match numpy's second-author reimplementation",
          "[fir_design][golden]") {
    for (const std::size_t n : { std::size_t{ 1023 }, std::size_t{ 4095 } }) {
        CAPTURE(n);
        const auto& golden = findCase(firGolden(), "fir_freqsamp_" + std::to_string(n));
        REQUIRE(golden.size == n);

        const auto result = designFir(boostAndCutTarget(), golden.row("sample_rate").front(), n,
                                      FirPhase::Linear);
        REQUIRE(result.taps.size() == n);
        REQUIRE(static_cast<double>(result.designFftSize) == golden.row("design_fft_size").front());

        const auto expected = golden.floatRow("taps");
        const double peak = peakAbs(expected);
        for (std::size_t i = 0; i < n; ++i) {
            const double residual = std::abs(static_cast<double>(result.taps[i]) -
                                             static_cast<double>(expected[i]));
            const double tol = shapeATolerance(expected[i], peak);
            CAPTURE(i, result.taps[i], expected[i], residual, tol);
            CHECK(residual <= tol);
        }
    }
}
