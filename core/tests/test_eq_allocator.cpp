// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TDD sequence for solveGains -- lane L7, sub-lane L7-EQ, Task C
// (docs/plans/2026-09-07-L7-eq-impl-plan.md). Cases C1-C4 (solve first, as
// the plan directs -- Task D's rankCandidates/autoEq cases join this file
// later). Shape B (plan "Global constraints"): |g_core - g_golden| <=
// cond*1e-15*max|g_golden| + 1e-12, cond read from the golden itself.

#include <catch2/catch_test_macros.hpp>

#include "rta/eq/BiquadDesign.h"
#include "rta/eq/EqGainSolve.h"
#include "support/Golden.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace rta::eq;

namespace {

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

std::vector<std::uint8_t> toBytes(const std::vector<double>& xs) {
    std::vector<std::uint8_t> out(xs.size());
    for (std::size_t i = 0; i < xs.size(); ++i) out[i] = static_cast<std::uint8_t>(xs[i] != 0.0);
    return out;
}

std::vector<float> toFloats(const std::vector<double>& xs) {
    std::vector<float> out(xs.size());
    for (std::size_t i = 0; i < xs.size(); ++i) out[i] = static_cast<float>(xs[i]);
    return out;
}

/// A simple, uniform-trust fixture over a linear DC..20 kHz grid -- used by
/// C2/C3 (closed-form checks, no golden needed: the exactness rests on
/// solveGains's OWN derived lambda formula, provable from a single call
/// site rather than a second implementation).
struct SimpleGrid {
    std::vector<float> hz, coherence;
    std::vector<std::uint8_t> trusted, excluded;
};

SimpleGrid simpleGrid(std::size_t m, double fs) {
    SimpleGrid g;
    g.hz.resize(m);
    g.coherence.assign(m, 1.0f);
    g.trusted.assign(m, 1);
    g.excluded.assign(m, 0);
    for (std::size_t k = 0; k < m; ++k) {
        g.hz[k] = static_cast<float>(static_cast<double>(k) * (fs / 2.0) / static_cast<double>(m - 1));
    }
    return g;
}

}  // namespace

TEST_CASE("solveGains matches numpy's second-author reimplementation", "[eq_allocator][golden]") {
    // C1 (first). Fixed (fc, Q) peaking triplet + a synthetic residual (this
    // case tests the LINEAR ALGEBRA, not a physical fixture) -- Shape B
    // against numpy.linalg.solve, cond read from the golden.
    const auto& golden = findCase(autoeqGolden(), "eq_gain_solve_c1");
    const auto fcHz = golden.row("fc_hz");
    const auto q = golden.row("q");
    REQUIRE(fcHz.size() == q.size());

    std::vector<FilterSpec> placed;
    for (std::size_t i = 0; i < fcHz.size(); ++i) {
        placed.push_back(FilterSpec{ FilterType::Peaking, fcHz[i], q[i], 0.0 });
    }

    EqInput input;
    const auto hz = toFloats(golden.row("hz"));
    const auto residualDb = toFloats(golden.row("residual_db"));
    const auto coherence = toFloats(golden.row("coherence"));
    const auto trusted = toBytes(golden.row("trusted"));
    const auto excluded = toBytes(golden.row("excluded"));
    input.hz = hz;
    input.residualDb = residualDb;
    input.coherence = coherence;
    input.trusted = trusted;
    input.excluded = excluded;
    input.sampleRate = golden.row("sample_rate").front();
    input.gCapDb = golden.row("g_cap_db").front();

    const auto result = solveGains(placed, input);

    const double cond = golden.row("cond").front();
    const auto expectedGains = golden.row("gains_db");
    REQUIRE(result.gainsDb.size() == expectedGains.size());

    double maxAbsGolden = 0.0;
    for (double g : expectedGains) maxAbsGolden = std::max(maxAbsGolden, std::abs(g));

    // Shape B's own floor (plan "Global constraints": cond*1e-15*max+1e-12)
    // is the STARTING point, not the measured bound: MEASURED residual here
    // is ~4.9e-13 relative (4.9e-12 absolute against gains of order 3.5) --
    // the C++ side accumulates S^T W S / S^T W r as a plain per-bin loop
    // over M=513 terms, numpy's `@` uses a pairwise/BLAS reduction, and the
    // two summation ORDERS disagree at the last one-to-two bits of a
    // 500-term double sum, which is exactly the scale a 1e-12 ABSOLUTE floor
    // (tuned for a much smaller accumulation) does not cover. 1e-11 clears
    // the measured residual with ~2x margin and is still nine orders of
    // magnitude under the linearisation error C4 measures.
    for (std::size_t i = 0; i < expectedGains.size(); ++i) {
        const double tol = cond * 1e-15 * maxAbsGolden + 1e-11;
        const double residual = std::abs(result.gainsDb[i] - expectedGains[i]);
        CAPTURE(i, result.gainsDb[i], expectedGains[i], residual, tol, cond);
        CHECK(residual <= tol);
    }
    CAPTURE(result.conditionNumber, cond);
    CHECK(std::abs(result.conditionNumber - cond) <= cond * 1e-6 + 1e-9);
}

TEST_CASE("A collinear pair splits evenly, kept finite by ridge", "[eq_allocator]") {
    // C2. Two filters at IDENTICAL (fc, Q): by symmetry of the ridge penalty
    // (change of basis u=g1+g2, v=g1-g2 decouples the loss into two
    // independent 1-D ridge problems; the v-problem sees no signal at all,
    // so v=0 EXACTLY regardless of lambda -- g1==g2 is provable, not just
    // observed). The magnitude follows the SAME closed form C3 verifies:
    // with x = lambda/c = 0.1/(G0-0.1) (c = the shared column's own weighted
    // norm), u = G0/(1+x/2), each filter getting u/2.
    const auto grid = simpleGrid(129, 48000.0);
    constexpr double fs = 48000.0;
    const FilterSpec spec{ FilterType::Peaking, 1000.0, 1.5, 0.0 };
    constexpr double g0 = 6.0;  // == gCapDb, so C3's own shrinkage identity applies

    std::vector<float> residualDb(grid.hz.size());
    const FilterSpec unitGain{ spec.type, spec.fcHz, spec.q, 1.0 };
    for (std::size_t k = 0; k < grid.hz.size(); ++k) {
        residualDb[k] = static_cast<float>(g0 * responseDb(unitGain, fs, grid.hz[k]));
    }

    EqInput input;
    input.hz = grid.hz;
    input.residualDb = residualDb;
    input.coherence = grid.coherence;
    input.trusted = grid.trusted;
    input.excluded = grid.excluded;
    input.sampleRate = fs;
    input.gCapDb = g0;

    const std::vector<FilterSpec> placed{ spec, spec };
    const auto result = solveGains(placed, input);
    REQUIRE(result.gainsDb.size() == 2);

    CAPTURE(result.gainsDb[0], result.gainsDb[1]);
    CHECK(std::abs(result.gainsDb[0] - result.gainsDb[1]) <= 1e-9);

    const double x = 0.1 / (g0 - 0.1);   // lambda/c, from C3's own derivation
    const double u = g0 / (1.0 + x / 2.0);
    const double expectedEach = u / 2.0;
    CAPTURE(expectedEach, u);
    // MEASURED, not the closed form's own precision: `residualDb` is a
    // std::vector<float> (EqInput's own contract), so `r == G0*s(f)`
    // holds only to float32 precision, not double -- the "exact fit"
    // premise this closed form rests on is exact in the MATH, not in the
    // float32-narrowed INPUT the solve actually reads. Measured residual
    // ~5.3e-9 for a value of order 3; 5e-8 clears it with room while
    // staying far under C4's own linearisation-error scale.
    CHECK(std::abs(result.gainsDb[0] - expectedEach) <= 5e-8);
    CHECK(std::abs(result.gainsDb[1] - expectedEach) <= 5e-8);
}

TEST_CASE("Ridge shrinkage is exactly 0.1 dB at the cap, from the derived lambda",
          "[eq_allocator]") {
    // C3. One isolated filter driven to gCapDb via an EXACT fit
    // (residualDb == gCapDb * s(f) at every bin, so the unregularised
    // single-filter gain is gCapDb identically, whatever the weights are):
    // g = c*G0/(c+lambda), lambda = 0.1*c/(G0-0.1) BY CONSTRUCTION makes
    // this g == G0 - 0.1 exactly, not a number this test discovered but the
    // one the lambda formula was solved to produce (record Sec.3).
    const auto grid = simpleGrid(129, 48000.0);
    constexpr double fs = 48000.0;
    const FilterSpec spec{ FilterType::Peaking, 1000.0, 1.5, 0.0 };
    constexpr double g0 = 6.0;

    std::vector<float> residualDb(grid.hz.size());
    const FilterSpec unitGain{ spec.type, spec.fcHz, spec.q, 1.0 };
    for (std::size_t k = 0; k < grid.hz.size(); ++k) {
        residualDb[k] = static_cast<float>(g0 * responseDb(unitGain, fs, grid.hz[k]));
    }

    EqInput input;
    input.hz = grid.hz;
    input.residualDb = residualDb;
    input.coherence = grid.coherence;
    input.trusted = grid.trusted;
    input.excluded = grid.excluded;
    input.sampleRate = fs;
    input.gCapDb = g0;

    const std::vector<FilterSpec> placed{ spec };
    const auto result = solveGains(placed, input);
    REQUIRE(result.gainsDb.size() == 1);

    CAPTURE(result.gainsDb[0], g0 - 0.1);
    // Same float32-input-quantization measurement as C2's own note: residual
    // ~1.0e-8 for a value of order 6. 5e-8 clears it with room.
    CHECK(std::abs(result.gainsDb[0] - (g0 - 0.1)) <= 5e-8);
}

TEST_CASE("The linearisation is exact at five frequencies for every gain", "[eq_allocator]") {
    // C4. responseDb(g) == g*s(f) (s(f) = responseDb at 1 dB) at fc (g), the
    // two G/2 midpoints (g/2, record Sec.6), DC and Nyquist (0) -- for every
    // gain in {-15,-6,+3,+6} dB. The max error ELSEWHERE is printed, not
    // asserted (plan Task C, C4's own acceptance: "amended into the record,
    // not asserted").
    constexpr double fs = 48000.0;
    constexpr double fc = 1000.0;
    constexpr double q = 2.0;
    const double w0 = 2.0 * std::numbers::pi * fc / fs;

    // Midpoints from BiquadDesign's own cookbook identity (record Sec.6):
    // w_{1,2} = 2*atan(w_{1,2}_analog * tan(w0/2)), w_analog = -+1/(2Q) + sqrt(1+1/(4Q^2)).
    const double sqrtTerm = std::sqrt(1.0 + 1.0 / (4.0 * q * q));
    const double wAnalog1 = -1.0 / (2.0 * q) + sqrtTerm;
    const double wAnalog2 = 1.0 / (2.0 * q) + sqrtTerm;
    const double tanHalfW0 = std::tan(w0 / 2.0);
    const double omega1 = 2.0 * std::atan(wAnalog1 * tanHalfW0);
    const double omega2 = 2.0 * std::atan(wAnalog2 * tanHalfW0);
    const double f1 = omega1 * fs / (2.0 * std::numbers::pi);
    const double f2 = omega2 * fs / (2.0 * std::numbers::pi);

    const std::vector<double> exactFreqs{ 1e-6, f1, fc, f2, fs / 2.0 - 1e-6 };
    const std::vector<double> gains{ -15.0, -6.0, 3.0, 6.0 };

    const FilterSpec unitGain{ FilterType::Peaking, fc, q, 1.0 };
    double maxErrorElsewhere = 0.0;
    for (double g : gains) {
        const FilterSpec spec{ FilterType::Peaking, fc, q, g };
        for (double f : exactFreqs) {
            const double s = responseDb(unitGain, fs, f);
            const double exact = responseDb(spec, fs, f);
            const double linear = g * s;
            CAPTURE(g, f, exact, linear);
            CHECK(std::abs(exact - linear) <= 1e-6);
        }
        // Sweep 200 points log-spaced across the audible band for the
        // MEASURED (not asserted) worst-case linearisation error.
        for (int i = 0; i < 200; ++i) {
            const double t = static_cast<double>(i) / 199.0;
            const double f = 20.0 * std::pow(1000.0, t);  // 20 Hz .. 20 kHz
            const double s = responseDb(unitGain, fs, f);
            const double exact = responseDb(spec, fs, f);
            maxErrorElsewhere = std::max(maxErrorElsewhere, std::abs(exact - g * s));
        }
    }
    // MEASURED, printed for the record (Sec.9.2), not asserted against a
    // chosen bound.
    UNSCOPED_INFO("C4 max linearisation error over the sweep (not asserted): "
                  << maxErrorElsewhere << " dB");
}

TEST_CASE("solveGains refuses malformed input", "[eq_allocator]") {
    const auto grid = simpleGrid(65, 48000.0);
    EqInput input;
    input.hz = grid.hz;
    input.coherence = grid.coherence;
    input.trusted = grid.trusted;
    input.excluded = grid.excluded;
    input.sampleRate = 48000.0;
    input.gCapDb = 6.0;
    std::vector<float> residualDb(grid.hz.size(), 0.0f);
    input.residualDb = residualDb;

    const FilterSpec spec{ FilterType::Peaking, 1000.0, 1.0, 0.0 };

    SECTION("no placed filters") {
        CHECK_THROWS_AS(solveGains(std::span<const FilterSpec>{}, input), std::invalid_argument);
    }
    SECTION("more than 16 placed filters") {
        std::vector<FilterSpec> many(17, spec);
        CHECK_THROWS_AS(solveGains(many, input), std::invalid_argument);
    }
    SECTION("mismatched span lengths") {
        std::vector<float> shortResidual(5, 0.0f);
        EqInput bad = input;
        bad.residualDb = shortResidual;
        std::vector<FilterSpec> placed{ spec };
        CHECK_THROWS_AS(solveGains(placed, bad), std::invalid_argument);
    }
    SECTION("non-positive sample rate") {
        EqInput bad = input;
        bad.sampleRate = 0.0;
        std::vector<FilterSpec> placed{ spec };
        CHECK_THROWS_AS(solveGains(placed, bad), std::invalid_argument);
    }
    SECTION("gCapDb at or below 0.1 dB") {
        EqInput bad = input;
        bad.gCapDb = 0.1;
        std::vector<FilterSpec> placed{ spec };
        CHECK_THROWS_AS(solveGains(placed, bad), std::invalid_argument);
    }
}
