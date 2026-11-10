// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H12 T2 (docs/dsp/2026-11-10-g22b-two-file-wav-drift.md E3): the Theil-Sen
// clock line. Closed forms and hand-computed values only; the phase-slope half
// of T2 lives in test_clock_drift.cpp.
#include "rta/dsp/ClockDrift.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace rta::dsp;

TEST_CASE("theil-sen: an exact line comes back exactly") {
    // delay = 0.25 + 1.5e-4 * position, positions 4096 apart. Each pairwise
    // slope is (d_j - d_i) / dp with d carrying one rounding u|d| apiece, so
    // |slope error| <= 2 u dmax / dpMin + u |slope|; intercept = median(d -
    // slope p): <= u dmax + u |slope p|max + |slope error| pMax.
    constexpr double u = 1.1102230246251565e-16;
    std::vector<DelayObservation> pts;
    for (int i = 0; i < 12; ++i) {
        const double p = 4096.0 * (i + 1);
        pts.push_back({p, 0.25 + 1.5e-4 * p});
    }
    const double dmax = pts.back().delaySamples, pmax = pts.back().positionSamples;
    const auto fit = fitDriftTheilSen(pts);
    REQUIRE(fit.has_value());
    const double slopeTol = 2.0 * u * dmax / 4096.0 + u * 1.5e-4 + 4.0 * u * 1.5e-4;
    const double interceptTol = u * dmax + u * 1.5e-4 * pmax + slopeTol * pmax + u;
    CHECK(std::fabs(fit->slope - 1.5e-4) <= slopeTol);
    CHECK(std::fabs(fit->intercept - 0.25) <= interceptTol);
    CHECK(fit->used == 12);
    CHECK(fit->maxAbsResidual <= interceptTol + slopeTol * pmax);
}

TEST_CASE("theil-sen: 25 percent gross outliers do not move the line") {
    constexpr double u = 1.1102230246251565e-16;
    std::vector<DelayObservation> pts;
    for (int i = 0; i < 12; ++i) {
        const double p = 4096.0 * (i + 1);
        pts.push_back({p, -0.5 + 8.0e-5 * p});
    }
    // 3 of 12 bad (25 %), on both sides and at both ends: pairs touching a bad
    // point are 66 - C(9,2) = 30 of 66 (45 %) < 50 %, so the median slope is a
    // clean pair's; likewise 25 % of intercepts. Clean slopes are all equal up
    // to rounding, so the answer is the line to rounding.
    pts[0].delaySamples += 7.0;
    pts[5].delaySamples -= 11.0;
    pts[11].delaySamples += 5.0;
    const auto fit = fitDriftTheilSen(pts);
    REQUIRE(fit.has_value());
    const double dmax = 0.5 + 8.0e-5 * 4096.0 * 12 + 11.0;
    const double slopeTol = 2.0 * u * dmax / 4096.0 + 5.0 * u * 8.0e-5;
    const double interceptTol = 2.0 * u * dmax + slopeTol * 4096.0 * 12;
    CHECK(std::fabs(fit->slope - 8.0e-5) <= slopeTol);
    CHECK(std::fabs(fit->intercept + 0.5) <= interceptTol);
    // The 3 outliers are what is left in the residuals.
    CHECK(std::fabs(fit->maxAbsResidual - 11.0) <= interceptTol + slopeTol * 4096.0 * 12);
}

TEST_CASE("theil-sen: residuals against hand-computed values") {
    // delays 0, 1, 1, 3 at positions 0..3. Pair slopes: 1, 0.5, 1, 0, 1, 2 ->
    // sorted 0, 0.5, 1, 1, 1, 2 -> median 1. Intercepts d - p: 0, 0, -1, 0 ->
    // median 0. Residuals 0, 0, -1, 0: rms = sqrt(1/4) = 0.5, max 1.
    const std::vector<DelayObservation> a{{0, 0}, {1, 1}, {2, 1}, {3, 3}};
    const auto fa = fitDriftTheilSen(a);
    REQUIRE(fa.has_value());
    CHECK(fa->slope == 1.0);
    CHECK(fa->intercept == 0.0);
    CHECK(fa->residualRms == 0.5);
    CHECK(fa->maxAbsResidual == 1.0);
    CHECK(fa->used == 4);
    // delays 0, 1, 3 at 0, 1, 2: slopes 1, 1.5, 2 -> 1.5; intercepts 0, -0.5, 0
    // -> 0; residuals 0, -0.5, 0: rms sqrt(0.25/3), max 0.5.
    const std::vector<DelayObservation> b{{0, 0}, {1, 1}, {2, 3}};
    const auto fb = fitDriftTheilSen(b);
    REQUIRE(fb.has_value());
    CHECK(fb->slope == 1.5);
    CHECK(fb->intercept == 0.0);
    CHECK(fb->residualRms == std::sqrt(0.25 / 3.0));
    CHECK(fb->maxAbsResidual == 0.5);
}

TEST_CASE("theil-sen: fewer than two points, or no spread in position, is refused") {
    CHECK_FALSE(fitDriftTheilSen({}).has_value());
    const std::vector<DelayObservation> one{{5, 1}};
    CHECK_FALSE(fitDriftTheilSen(one).has_value());
    const std::vector<DelayObservation> same{{5, 1}, {5, 2}, {5, 3}};
    CHECK_FALSE(fitDriftTheilSen(same).has_value());
}
