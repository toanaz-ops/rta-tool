// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TDD sequence for rankCandidates/autoEq -- lane L7, sub-lane L7-EQ, Task D
// (docs/plans/2026-09-07-L7-eq-impl-plan.md). Cases D1-D6. Split from
// test_eq_allocator.cpp (Task C) once the pair would have crossed the
// plan's own 340-line cap for the file (CLAUDE.md: "a file past that is
// doing more than one job -- split it along the seam that made it long";
// solve vs. placement is exactly that seam).

#include <catch2/catch_test_macros.hpp>

#include "rta/eq/BiquadDesign.h"
#include "rta/eq/EqAllocator.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <stdexcept>
#include <vector>

using namespace rta::eq;

namespace {

constexpr double kPi = std::numbers::pi;

struct Fixture {
    std::vector<float> hz, residualDb, coherence;
    std::vector<std::uint8_t> trusted, excluded;
    std::vector<std::complex<double>> h;  // empty when the G24 gate is not needed
};

Fixture flatGrid(std::size_t m, double fs) {
    Fixture f;
    f.hz.resize(m);
    f.residualDb.assign(m, 0.0f);
    f.coherence.assign(m, 1.0f);
    f.trusted.assign(m, 1);
    f.excluded.assign(m, 0);
    for (std::size_t k = 0; k < m; ++k) {
        f.hz[k] = static_cast<float>(static_cast<double>(k) * (fs / 2.0) / static_cast<double>(m - 1));
    }
    return f;
}

EqInput makeInput(const Fixture& f, double fs, int maxFilters = 6, double gCapDb = 6.0) {
    EqInput input;
    input.hz = f.hz;
    input.residualDb = f.residualDb;
    input.coherence = f.coherence;
    input.trusted = f.trusted;
    input.excluded = f.excluded;
    input.sampleRate = fs;
    input.maxFilters = maxFilters;
    input.gCapDb = gCapDb;
    if (!f.h.empty()) input.hHalfGrid = f.h;
    return input;
}

}  // namespace

TEST_CASE("Greedy placement lands on a known Gaussian bump's centre and half-depth Q",
          "[eq_allocator_placement]") {
    // D1 (first). A single dip of known centre fc0 and known sigma: the
    // placement bin lands on fc0 (to the grid's resolution) and
    // Q = fc/(f2-f1) matches the Gaussian's own analytic FWHM-derived Q.
    constexpr double fs = 48000.0;
    constexpr std::size_t m = 1025;
    constexpr double fc0 = 2000.0;
    constexpr double sigma = 100.0;

    auto fixture = flatGrid(m, fs);
    for (std::size_t k = 0; k < m; ++k) {
        const double df = fixture.hz[k] - fc0;
        fixture.residualDb[k] = static_cast<float>(-6.0 * std::exp(-(df * df) / (2.0 * sigma * sigma)));
    }

    const auto input = makeInput(fixture, fs);
    const auto candidates = rankCandidates(input, 1);
    REQUIRE(candidates.size() == 1);

    const double binWidth = (fs / 2.0) / static_cast<double>(m - 1);
    CAPTURE(candidates[0].spec.fcHz, fc0, binWidth);
    CHECK(std::abs(candidates[0].spec.fcHz - fc0) <= binWidth);

    const double fwhm = 2.0 * sigma * std::sqrt(2.0 * std::log(2.0));
    const double expectedQ = fc0 / fwhm;
    CAPTURE(candidates[0].spec.q, expectedQ);
    CHECK(std::abs(candidates[0].spec.q - expectedQ) <= 0.3 * expectedQ);  // bin-resolution crossing, not analytic
    CHECK(candidates[0].spec.gainDb > 0.0);  // a dip below target is boosted
}

TEST_CASE("An excluded region is not proposed", "[eq_allocator_placement]") {
    // D2. The same bump, but its whole region is declined (record Sec.2) --
    // no candidate is proposed there.
    constexpr double fs = 48000.0;
    constexpr std::size_t m = 1025;
    constexpr double fc0 = 2000.0;
    constexpr double sigma = 100.0;

    // The bump is written ONLY inside its own exclusion window -- every
    // other bin stays at flatGrid's exact 0.0f default. A Gaussian's tail is
    // never exactly zero, so writing it everywhere and excluding only the
    // centre would leave the tail JUST past the boundary as the largest
    // remaining nonzero value in the fixture, which would register as "the"
    // extremum in its own right -- exactly the failure this fixture must
    // NOT produce, since excluded is the thing under test here, not a tail
    // the fixture forgot to clip.
    auto fixture = flatGrid(m, fs);
    for (std::size_t k = 0; k < m; ++k) {
        if (std::abs(fixture.hz[k] - fc0) <= 4.0 * sigma) {
            const double df = fixture.hz[k] - fc0;
            fixture.residualDb[k] =
                static_cast<float>(-6.0 * std::exp(-(df * df) / (2.0 * sigma * sigma)));
            fixture.excluded[k] = 1;
        }
    }

    const auto input = makeInput(fixture, fs);
    const auto candidates = rankCandidates(input, 3);
    CHECK(candidates.empty());  // nothing else in this flat fixture to place on
}

TEST_CASE("The auto-offset is the closed-form gamma^2/f-weighted mean", "[eq_allocator_placement]") {
    // D3. Non-uniform coherence makes the weighted mean differ from a plain
    // one; the placement's own score (|workingResidual| at fStar) must
    // match residualDb[fStar] - c_expected, c_expected computed
    // independently here by the SAME closed form.
    constexpr double fs = 48000.0;
    constexpr std::size_t m = 513;
    constexpr double baseline = 2.0;
    constexpr double fc0 = 5000.0;
    constexpr double sigma = 150.0;

    auto fixture = flatGrid(m, fs);
    for (std::size_t k = 0; k < m; ++k) {
        fixture.residualDb[k] = static_cast<float>(baseline);
        // Coherence rises with frequency, so the weighted mean is pulled
        // toward the HIGH-frequency bins' own value -- still `baseline`
        // here (uniform), so this alone would not distinguish the formula;
        // the bump below is what the weighting must correctly discount by
        // its own small 1/f-weighted mass.
        fixture.coherence[k] = static_cast<float>(0.3 + 0.6 * static_cast<double>(k) / (m - 1));
        const double df = fixture.hz[k] - fc0;
        fixture.residualDb[k] += static_cast<float>(-8.0 * std::exp(-(df * df) / (2.0 * sigma * sigma)));
    }

    double num = 0.0, den = 0.0;
    for (std::size_t k = 0; k < m; ++k) {
        if (fixture.hz[k] <= 0.0f) continue;
        const double w = static_cast<double>(fixture.coherence[k]) / static_cast<double>(fixture.hz[k]);
        num += w * static_cast<double>(fixture.residualDb[k]);
        den += w;
    }
    const double cExpected = num / den;

    const auto input = makeInput(fixture, fs);
    const auto candidates = rankCandidates(input, 1);
    REQUIRE(candidates.size() == 1);

    std::size_t fStarIdx = 0;
    double best = -1.0;
    for (std::size_t k = 0; k < m; ++k) {
        if (std::abs(fixture.hz[k] - candidates[0].spec.fcHz) < best || best < 0.0) {
            const double d = std::abs(fixture.hz[k] - candidates[0].spec.fcHz);
            if (best < 0.0 || d < best) { best = d; fStarIdx = k; }
        }
    }
    const double expectedScore = std::abs(static_cast<double>(fixture.residualDb[fStarIdx]) - cExpected);
    CAPTURE(candidates[0].scoreDb, expectedScore, cExpected);
    CHECK(std::abs(candidates[0].scoreDb - expectedScore) <= 1e-4);
}

TEST_CASE("An NMP boost is refused, the same allocator still cuts a peak", "[eq_allocator_placement]") {
    // D4. The two-path comb (a=1.25, D=144 samples, 48 kHz -- the record's
    // own fixture, coherent per record Sec.9.5): every dip is
    // NotMinimumPhase, every peak is an ordinary cut. WITHOUT the G24 gate
    // (no hHalfGrid) the first placement is a BOOST at a dip; WITH it, every
    // dip is refused and the first placement is a CUT at a peak instead --
    // the gate visibly changing the outcome on the identical curve.
    constexpr double fs = 48000.0;
    constexpr std::size_t nFft = 16384;
    constexpr std::size_t m = nFft / 2 + 1;
    constexpr double a = 1.25;
    constexpr double d = 144.0;

    Fixture fixture;
    fixture.hz.resize(m);
    fixture.residualDb.resize(m);
    fixture.coherence.assign(m, 1.0f);
    fixture.trusted.assign(m, 1);
    fixture.excluded.assign(m, 0);
    fixture.h.resize(m);
    const double binWidthHz = fs / static_cast<double>(nFft);
    for (std::size_t k = 0; k < m; ++k) {
        const double theta = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(nFft);
        const std::complex<double> hk = 1.0 + a * std::polar(1.0, -theta * d);
        fixture.h[k] = hk;
        fixture.hz[k] = static_cast<float>(static_cast<double>(k) * binWidthHz);
        fixture.residualDb[k] = static_cast<float>(20.0 * std::log10(std::max(std::abs(hk), 1e-6)));
    }

    // Placement location and verdict are read directly off rankCandidates,
    // NOT off a solved gain: this fixture is a 72-period comb, and a SINGLE
    // filter's least-squares fit against the WHOLE 8192-bin oscillating
    // curve does not reproduce one period's own local height (the fit
    // problem solveGains solves is global, by design -- Task C's own
    // fixtures are deliberately not a many-period comb). What D4 actually
    // asserts -- where the allocator places, and why -- is fully decided at
    // the PLACEMENT stage, before any solve runs.
    auto nearestResidualDb = [&](double fcHz) {
        std::size_t best = 0;
        double bestDist = std::abs(static_cast<double>(fixture.hz[0]) - fcHz);
        for (std::size_t k = 1; k < m; ++k) {
            const double dist = std::abs(static_cast<double>(fixture.hz[k]) - fcHz);
            if (dist < bestDist) { bestDist = dist; best = k; }
        }
        return static_cast<double>(fixture.residualDb[best]);
    };

    auto withoutGate = fixture;
    withoutGate.h.clear();
    const auto inputWithoutGate = makeInput(withoutGate, fs, 1);
    const auto withoutGateCandidates = rankCandidates(inputWithoutGate, 1);
    REQUIRE(withoutGateCandidates.size() == 1);
    const double withoutGateResidual = nearestResidualDb(withoutGateCandidates[0].spec.fcHz);
    CAPTURE(withoutGateCandidates[0].spec.fcHz, withoutGateResidual);
    CHECK(withoutGateResidual < 0.0);  // placed on the (unrefused) DIP itself

    const auto inputWithGate = makeInput(fixture, fs, 1);
    const auto withGateCandidates = rankCandidates(inputWithGate, 1);
    REQUIRE(withGateCandidates.size() == 1);
    const double withGateResidual = nearestResidualDb(withGateCandidates[0].spec.fcHz);
    CAPTURE(withGateCandidates[0].spec.fcHz, withGateResidual, withGateCandidates[0].dip.swingRad,
            withGateCandidates[0].dip.thresholdRad);
    CHECK(withGateResidual > 0.0);  // every dip refused; placed on a PEAK instead
    CHECK(withoutGateCandidates[0].spec.fcHz != withGateCandidates[0].spec.fcHz);
}

TEST_CASE("The whole pipeline lowers the weighted-RMS residual (labelled regression lock)",
          "[eq_allocator_placement]") {
    // D5. A three-peak fixture: autoEq(N=3)'s gamma^2/f-weighted RMS
    // residual, AFTER subtracting the solved cascade's own linear-model
    // response, is below the input's own. Labelled a lock, not a proof
    // (CLAUDE.md).
    constexpr double fs = 48000.0;
    constexpr std::size_t m = 1025;
    const double centres[] = { 500.0, 3000.0, 9000.0 };
    const double gains[] = { -5.0, 4.0, -3.0 };
    constexpr double sigma = 200.0;

    auto fixture = flatGrid(m, fs);
    for (std::size_t k = 0; k < m; ++k) {
        double v = 0.0;
        for (int i = 0; i < 3; ++i) {
            const double df = fixture.hz[k] - centres[i];
            v += gains[i] * std::exp(-(df * df) / (2.0 * sigma * sigma));
        }
        fixture.residualDb[k] = static_cast<float>(v);
    }

    const auto input = makeInput(fixture, fs, 3);
    const auto result = autoEq(input);
    REQUIRE(result.size() == 3);

    auto weightedRms = [&](auto residualAt) {
        double num = 0.0, den = 0.0;
        for (std::size_t k = 0; k < m; ++k) {
            if (!(fixture.hz[k] > 0.0f)) continue;
            const double w = static_cast<double>(fixture.coherence[k]) / static_cast<double>(fixture.hz[k]);
            const double r = residualAt(k);
            num += w * r * r;
            den += w;
        }
        return std::sqrt(num / den);
    };

    // EQ-R3's own ghost identity: ghost_k = m_k + sum R_i(f_k). Expressed on
    // the residual (m - t) directly, the predicted POST-eq residual is
    // residual + sum R_i(f_k) -- ADDED, matching EqAllocator.cpp's own
    // negated() doc comment on why the joint solve is handed -residual, not
    // +residual (a dip's own filter response comes out POSITIVE, boosting,
    // and adding it here is what makes the residual shrink toward zero).
    const double rmsBefore = weightedRms([&](std::size_t k) { return static_cast<double>(fixture.residualDb[k]); });
    const double rmsAfter = weightedRms([&](std::size_t k) {
        double predicted = static_cast<double>(fixture.residualDb[k]);
        for (const auto& spec : result) predicted += responseDb(spec, fs, fixture.hz[k]);
        return predicted;
    });

    CAPTURE(rmsBefore, rmsAfter);
    CHECK(rmsAfter < rmsBefore);
}

TEST_CASE("rankCandidates/autoEq refuse malformed input and degrade honestly otherwise",
          "[eq_allocator_placement]") {
    // D6. fs<=0 and mismatched spans throw; every-bin-untrusted or an N
    // above the number of available extrema return fewer results, never a
    // curve built from nothing (memory/
    // a-fixed-defect-returns-through-the-silent-fallback.md).
    constexpr double fs = 48000.0;
    auto fixture = flatGrid(65, fs);

    SECTION("non-positive sample rate") {
        auto input = makeInput(fixture, 0.0);
        CHECK_THROWS_AS(rankCandidates(input, 1), std::invalid_argument);
        CHECK_THROWS_AS(autoEq(input), std::invalid_argument);
    }
    SECTION("mismatched span lengths") {
        auto input = makeInput(fixture, fs);
        std::vector<float> shortResidual(5, 0.0f);
        input.residualDb = shortResidual;
        CHECK_THROWS_AS(rankCandidates(input, 1), std::invalid_argument);
    }
    SECTION("every bin untrusted returns empty, not a throw") {
        std::fill(fixture.trusted.begin(), fixture.trusted.end(), std::uint8_t{ 0 });
        auto input = makeInput(fixture, fs);
        CHECK(rankCandidates(input, 3).empty());
        CHECK(autoEq(input).empty());
    }
    SECTION("N above the available extrema returns what it could place, not a throw") {
        // One dip, three flat placements' worth of room requested.
        for (std::size_t k = 0; k < fixture.hz.size(); ++k) {
            const double df = fixture.hz[k] - 5000.0;
            fixture.residualDb[k] = static_cast<float>(-6.0 * std::exp(-(df * df) / (2.0 * 300.0 * 300.0)));
        }
        auto input = makeInput(fixture, fs, 6);
        const auto result = autoEq(input);
        CHECK(result.size() < 6);
        CHECK_FALSE(result.empty());
    }
}
