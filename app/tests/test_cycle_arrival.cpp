// SPDX-License-Identifier: AGPL-3.0-or-later
//
// CycleArrival.h (PR #72 verifier round 1): the peak lag an arrival carries, and
// the guard that refuses an integer lying between candidates.

#include "measure/CycleArrival.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <optional>
#include <vector>

using rta::measure::candidateWithinGuard;
using rta::measure::CrossoverFamily;
using rta::measure::Topology;
using rta::measure::topologyPeakLagSamples;

namespace {
constexpr double kFs = 48000.0;
double lowPassLag(CrossoverFamily family, int order, double fc) {
    return topologyPeakLagSamples(Topology{ family, order }, false, fc, kFs);
}
}  // namespace

TEST_CASE("peak lag: LR2 low-pass has the closed form tau = 1/(2 pi fc)", "[cycle_arrival]") {
    // LR2 is a first-order low-pass twice: H = 1/(tau s + 1)^2, h(t) = (t/tau^2) e^(-t/tau),
    // which peaks at t = tau exactly. Two samples of slack for the bilinear transform.
    const double tauSamples = kFs / (2.0 * std::numbers::pi * 100.0);  // 76.39
    CHECK(std::fabs(lowPassLag(CrossoverFamily::LinkwitzRiley, 2, 100.0) - tauSamples) <= 2.0);
}

TEST_CASE("peak lag: BW1 low-pass peaks at the wavefront, h = e^(-t/tau)/tau", "[cycle_arrival]") {
    CHECK(lowPassLag(CrossoverFamily::Butterworth, 1, 100.0) <= 1.0);
}

TEST_CASE("peak lag: grows with order and scales as 1/fc", "[cycle_arrival]") {
    const double lr2 = lowPassLag(CrossoverFamily::LinkwitzRiley, 2, 100.0);
    const double lr4 = lowPassLag(CrossoverFamily::LinkwitzRiley, 4, 100.0);
    const double lr8 = lowPassLag(CrossoverFamily::LinkwitzRiley, 8, 100.0);
    CHECK(lr2 < lr4);
    CHECK(lr4 < lr8);
    // Regression lock, NOT a standard: the PR #72 verifier measured LR4 at about
    // 4.60 ms and LR8 at 9.44 ms for a 100 Hz corner; these are those figures at 48 kHz.
    CHECK(std::fabs(lr4 - 4.60e-3 * kFs) <= 3.0);
    CHECK(std::fabs(lr8 - 9.44e-3 * kFs) <= 4.0);
    CHECK(std::fabs(lowPassLag(CrossoverFamily::LinkwitzRiley, 4, 50.0) - 2.0 * lr4) <= 3.0);
}

TEST_CASE("peak lag: a high-pass lags about nothing; unusable arguments correct nothing", "[cycle_arrival]") {
    for (const int order : { 2, 4, 8 }) {
        CHECK(topologyPeakLagSamples({ CrossoverFamily::LinkwitzRiley, order }, true, 100.0, kFs) <= 2.0);
    }
    CHECK(topologyPeakLagSamples({ CrossoverFamily::LinkwitzRiley, 4 }, false, 0.0, kFs) == 0.0);
    CHECK(topologyPeakLagSamples({ CrossoverFamily::LinkwitzRiley, 4 }, false, 100.0, 0.0) == 0.0);
    CHECK(topologyPeakLagSamples({ CrossoverFamily::LinkwitzRiley, 3 }, false, 100.0, kFs) == 0.0);  // no odd LR
    CHECK(topologyPeakLagSamples({ CrossoverFamily::Butterworth, 40 }, false, 100.0, kFs) == 0.0);
}

TEST_CASE("guard: nearest candidate only within a quarter of the spacing", "[cycle_arrival]") {
    // The candidates the PR #72 verifier measured on the fixture, in seconds.
    const std::vector<double> candidates{ 2.146e-3, 12.987e-3, -8.980e-3 };
    const double spacing = 12.987e-3 - 2.146e-3;  // 10.841 ms, the smaller of the two gaps
    CHECK(candidateWithinGuard(candidates, 2.5e-3) == std::optional<double>(2.146e-3));
    CHECK(candidateWithinGuard(candidates, 2.146e-3 + spacing / 4.0 - 1e-6).has_value());
    CHECK_FALSE(candidateWithinGuard(candidates, 2.146e-3 + spacing / 4.0 + 1e-6).has_value());
    // The verifier's refuting integer: 6.58 ms sits 4.43 ms from the right candidate.
    CHECK_FALSE(candidateWithinGuard(candidates, 6.58e-3).has_value());
    CHECK_FALSE(candidateWithinGuard(candidates, 7.57e-3).has_value());  // the midpoint
    CHECK(candidateWithinGuard({ 1.0e-3 }, 50.0e-3) == std::optional<double>(1.0e-3));  // nothing to confuse it with
    CHECK_FALSE(candidateWithinGuard({}, 1.0e-3).has_value());
}
