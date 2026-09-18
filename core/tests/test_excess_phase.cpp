// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TDD sequence for excessPhase (G24 kernel) -- lane L7, sub-lane L7-EQ, Task A
// (docs/plans/2026-09-07-L7-eq-impl-plan.md). Cases A1-A4.
//
// Tolerance shapes: quantities that pass through Fft (via
// minimumPhaseFromMagnitude) follow Shape A (memory/float32-fft-precision.md):
// tol_k = 1e-6*|expected_k| + c*peak(expected), residual captured beside the
// tolerance. A2/A4's fixtures also carry the log-f/linear-dB INTERPOLATION
// error the oversampling step introduces (ExcessPhase.h step 2) -- a second,
// separate error source the plan asks the golden to report, not fold into
// Shape A's FFT-only bound; this file states each fixture's own measured
// tolerance rather than reusing test_minimum_phase.cpp's numbers blind.

#include <catch2/catch_test_macros.hpp>

#include "rta/dsp/ExcessPhase.h"
#include "rta/eq/BiquadDesign.h"
#include "support/Golden.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

using namespace rta::dsp;
using rta::eq::designBiquad;
using rta::eq::FilterSpec;
using rta::eq::FilterType;

namespace {

constexpr double kPi = std::numbers::pi;

const std::vector<rta::test::GoldenCase>& autoeqGolden() {
    static const auto cases = rta::test::loadGolden(std::string(RTA_GOLDEN_DIR) + "/autoeq.txt");
    return cases;
}

// `name` is a by-value view, not `const std::string&`: gcc's -Wdangling-reference
// heuristic flags any reference-returning call that binds a temporary to a
// reference parameter, and the string built from a literal here is compared,
// never returned. A view has no reference for the heuristic to trip on and
// skips the allocation.
const rta::test::GoldenCase& findCase(const std::vector<rta::test::GoldenCase>& cases,
                                      std::string_view name) {
    for (const auto& c : cases) {
        if (c.name == name) return c;
    }
    throw std::runtime_error("golden case not found: " + std::string(name));
}

/// Builds a DC..Nyquist half-grid (m = nFft/2 + 1 bins) of a pure delay,
/// |H| == 1 identically, h_k = exp(-i*omega_k*delaySec) -- A1's fixture.
struct HalfGrid {
    std::vector<float> magnitude;
    std::vector<std::complex<double>> h;
    std::vector<std::uint8_t> trusted;
    double sampleRate = 0.0;
};

HalfGrid pureDelayGrid(std::size_t nFft, double sampleRate, double delaySec) {
    HalfGrid g;
    const std::size_t m = nFft / 2 + 1;
    g.magnitude.assign(m, 1.0f);
    g.h.resize(m);
    g.trusted.assign(m, 1);
    g.sampleRate = sampleRate;
    const double binWidthHz = sampleRate / static_cast<double>(nFft);
    for (std::size_t k = 0; k < m; ++k) {
        const double omega = 2.0 * kPi * static_cast<double>(k) * binWidthHz;
        g.h[k] = std::polar(1.0, -omega * delaySec);
    }
    return g;
}

/// A cookbook peaking section's own exact response -- min-phase by
/// construction (record Sec.9.3). Evaluated directly from Biquad::Coeffs
/// rather than via responseDb/attenuationDb, so this fixture does not
/// secretly depend on the sign convention those two apply.
HalfGrid peakingGrid(std::size_t nFft, double sampleRate, const FilterSpec& spec) {
    HalfGrid g;
    const std::size_t m = nFft / 2 + 1;
    g.magnitude.resize(m);
    g.h.resize(m);
    g.trusted.assign(m, 1);
    g.sampleRate = sampleRate;
    const auto c = designBiquad(spec, sampleRate);
    const double binWidthHz = sampleRate / static_cast<double>(nFft);
    for (std::size_t k = 0; k < m; ++k) {
        const double omega = 2.0 * kPi * static_cast<double>(k) * binWidthHz / sampleRate;
        const std::complex<double> num =
            c.b0 + c.b1 * std::polar(1.0, -omega) + c.b2 * std::polar(1.0, -2.0 * omega);
        const std::complex<double> den =
            1.0 + c.a1 * std::polar(1.0, -omega) + c.a2 * std::polar(1.0, -2.0 * omega);
        const std::complex<double> h = num / den;
        g.h[k] = h;
        g.magnitude[k] = static_cast<float>(std::abs(h));
    }
    return g;
}

}  // namespace

TEST_CASE("A pure delay has flat excess phase and recovers its own delay",
          "[excess_phase]") {
    // A1 (first). |H| == 1 identically (all trusted): the reconstructed
    // excess phase collapses to 0 after the broadband delay tau_0 = D/fs is
    // removed, and tau_0 itself matches D/fs within 1/fs (plan Task A's own
    // acceptance). A small explicit oversampleFactor is enough here --
    // flat magnitude reconstructs exactly regardless of factor (T1 of
    // test_minimum_phase.cpp), so this fixture cannot exercise aliasing;
    // kExcessPhaseOversamplingFactor itself is A3's job, not A1's.
    constexpr std::size_t nFft = 8192;
    constexpr double sampleRate = 48000.0;
    constexpr double delaySamples = 10.0;
    const double delaySec = delaySamples / sampleRate;

    const auto grid = pureDelayGrid(nFft, sampleRate, delaySec);
    const auto result = excessPhase(grid.magnitude, grid.h, grid.trusted, sampleRate,
                                    /*oversampleFactor=*/4);

    REQUIRE(result.valid);
    CAPTURE(result.broadbandDelaySec, delaySec);
    CHECK(std::abs(result.broadbandDelaySec - delaySec) <= 1.0 / sampleRate);

    // MEASURED (not shapeATolerance's pure-FFT bound): groupDelaySeconds is a
    // CENTRAL-DIFFERENCE estimate of an analytic derivative, so it carries a
    // small O((omega*binWidth*delaySec)^2) bias -- present at EVERY bin, but
    // the bins nearest Nyquist fall back to a widest-available ONE-SIDED
    // difference (GroupDelay.h's own documented edge behaviour), whose bias
    // differs from the interior bins' the trusted-median removes. Measured
    // max residual at the edge bins is ~3.1e-4 rad; 1e-3 rad clears it with
    // >3x margin while still being two-plus orders of magnitude under the
    // shallowest NMP swing this lane gates on (record Sec.4.3.6: 20 deg =
    // 0.35 rad).
    constexpr double kMeasuredResidualTolRad = 1e-3;
    for (std::size_t k = 0; k < result.excessPhaseRad.size(); ++k) {
        CAPTURE(k, result.excessPhaseRad[k], kMeasuredResidualTolRad);
        CHECK(std::abs(static_cast<double>(result.excessPhaseRad[k])) <= kMeasuredResidualTolRad);
    }
}

TEST_CASE("A cookbook peaking section (already minimum phase) reconstructs itself",
          "[excess_phase]") {
    // A2. arg(h_min) must match arg(h) itself: the excess phase is ~0 and
    // its swing is ~0. This fixture carries a real, measured interpolation
    // error on top of Shape A's pure-FFT bound (ExcessPhase.h step 2), so
    // the tolerance is stated and measured here, not borrowed from
    // test_minimum_phase.cpp's FFT-only fixtures.
    constexpr std::size_t nFft = 4096;
    constexpr double sampleRate = 48000.0;
    const FilterSpec spec{ FilterType::Peaking, /*fcHz=*/1000.0, /*q=*/1.0, /*gainDb=*/6.0 };

    const auto grid = peakingGrid(nFft, sampleRate, spec);
    const auto result = excessPhase(grid.magnitude, grid.h, grid.trusted, sampleRate,
                                    /*oversampleFactor=*/16);

    REQUIRE(result.valid);

    double maxAbs = 0.0;
    for (float phi : result.excessPhaseRad) maxAbs = std::max(maxAbs, std::abs(static_cast<double>(phi)));
    CAPTURE(maxAbs);
    // A moderate-Q peaking section's magnitude is smooth on a log-f axis, so
    // the interpolation step (linear in log10(f), linear in dB) tracks it
    // closely; measured residual is on the order of 1e-3 rad, two-plus
    // orders of magnitude under the shallowest NMP swing this lane gates on
    // (record Sec.4.3.6's table starts at 20 deg = 0.35 rad).
    CHECK(maxAbs <= 0.02);
}

TEST_CASE("The measured oversampling factor clears the sweep and is not silently 8",
          "[excess_phase][golden]") {
    // A3. Reads tools/gen_autoeq.py's sweep straight from the golden: the
    // shipped C++ constant must be >= the measured minimum, and every
    // fixture's own recorded min_factor (and every larger swept factor) must
    // actually be within SWING_CONVERGE_DEG of the F=256 reference -- that
    // is what "converged" MEANS, and it is what chose
    // kExcessPhaseOversamplingFactor. This is deliberately NOT a claim that
    // the distance is monotone non-increasing across the WHOLE sweep: the
    // measured table shows it is not (fixture i=2, a=1.25/D=511, distance
    // rises from 13.5 to 25.9 deg going from F=1 to F=2 before falling
    // through F=64 -- cepstral aliasing at a small factor is not a smooth
    // function of F, only an eventually-converging one).
    const auto& golden = findCase(autoeqGolden(), "eq_oversampling_sweep");
    const auto factors = golden.row("sweep_factors");
    const auto measuredMinimum = static_cast<std::size_t>(golden.row("measured_minimum_factor").front());
    const auto chosen = static_cast<std::size_t>(golden.row("chosen_oversampling_factor").front());
    const double swingConvergeDeg = golden.row("swing_converge_deg").front();

    CAPTURE(measuredMinimum, chosen, kExcessPhaseOversamplingFactor);
    CHECK(kExcessPhaseOversamplingFactor >= measuredMinimum);
    // The whole point of Task A: FIR's factor is not silently reused.
    CHECK(kExcessPhaseOversamplingFactor > 8);
    CHECK(chosen == kExcessPhaseOversamplingFactor);

    for (int i = 0; i < 9; ++i) {
        const auto swings = golden.row("swings_i" + std::to_string(i));
        const auto minFactor = static_cast<std::size_t>(golden.row("min_factor_i" + std::to_string(i)).front());
        REQUIRE(swings.size() == factors.size());
        const double reference = swings.back();
        for (std::size_t f = 0; f < factors.size(); ++f) {
            if (static_cast<std::size_t>(factors[f]) < minFactor) continue;
            const double distance = std::abs(swings[f] - reference);
            CAPTURE(i, f, factors[f], minFactor, distance, swingConvergeDeg);
            CHECK(distance < swingConvergeDeg);
        }
    }
}

TEST_CASE("Too few trusted bins refuse rather than produce a curve", "[excess_phase]") {
    // A4. A two-path comb fixture (so the scenario is a real notch, not an
    // arbitrary array) where only a single bin is trusted -- far too few to
    // establish a broadband-delay median or a meaningful swing anywhere,
    // including at the notch's own region. valid must be false, never a
    // curve built from one point (memory/
    // a-fixed-defect-returns-through-the-silent-fallback.md).
    constexpr std::size_t nFft = 2048;
    constexpr double sampleRate = 48000.0;
    constexpr double a = 1.25;
    constexpr double d = 144.0;

    const std::size_t m = nFft / 2 + 1;
    std::vector<float> magnitude(m);
    std::vector<std::complex<double>> h(m);
    std::vector<std::uint8_t> trusted(m, 0);
    for (std::size_t k = 0; k < m; ++k) {
        const double theta = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(nFft);
        const std::complex<double> hk = 1.0 + a * std::polar(1.0, -theta * d);
        h[k] = hk;
        magnitude[k] = static_cast<float>(std::abs(hk));
    }
    trusted[0] = 1;  // exactly one trusted bin: below kMinTrustedBins

    const auto result = excessPhase(magnitude, h, trusted, sampleRate, /*oversampleFactor=*/8);
    CHECK_FALSE(result.valid);
    CHECK(result.excessPhaseRad.empty());
    CHECK(result.broadbandDelaySec == 0.0);
}

TEST_CASE("excessPhase refuses malformed input", "[excess_phase]") {
    std::vector<float> magnitude(129, 1.0f);
    std::vector<std::complex<double>> h(129, std::complex<double>(1.0, 0.0));
    std::vector<std::uint8_t> trusted(129, 1);

    SECTION("mismatched span lengths") {
        std::vector<std::uint8_t> shortTrusted(10, 1);
        CHECK_THROWS_AS(excessPhase(magnitude, h, shortTrusted, 48000.0, 8), std::invalid_argument);
    }
    SECTION("size not 2^k+1") {
        std::vector<float> badMag(100, 1.0f);
        std::vector<std::complex<double>> badH(100, std::complex<double>(1.0, 0.0));
        std::vector<std::uint8_t> badTrusted(100, 1);
        CHECK_THROWS_AS(excessPhase(badMag, badH, badTrusted, 48000.0, 8), std::invalid_argument);
    }
    SECTION("non-positive sample rate") {
        CHECK_THROWS_AS(excessPhase(magnitude, h, trusted, 0.0, 8), std::invalid_argument);
    }
    SECTION("oversampleFactor not a power of two") {
        CHECK_THROWS_AS(excessPhase(magnitude, h, trusted, 48000.0, 3), std::invalid_argument);
    }
    SECTION("oversampleFactor zero") {
        CHECK_THROWS_AS(excessPhase(magnitude, h, trusted, 48000.0, 0), std::invalid_argument);
    }
}
