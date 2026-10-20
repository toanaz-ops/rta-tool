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
    constexpr double kRange = 20.0e-3;  // irrelevant with two or more candidates
    CHECK(candidateWithinGuard(candidates, 2.5e-3, kRange) == std::optional<double>(2.146e-3));
    CHECK(candidateWithinGuard(candidates, 2.146e-3 + spacing / 4.0 - 1e-6, kRange).has_value());
    CHECK_FALSE(candidateWithinGuard(candidates, 2.146e-3 + spacing / 4.0 + 1e-6, kRange).has_value());
    // The verifier's refuting integer: 6.58 ms sits 4.43 ms from the right candidate.
    CHECK_FALSE(candidateWithinGuard(candidates, 6.58e-3, kRange).has_value());
    CHECK_FALSE(candidateWithinGuard(candidates, 7.57e-3, kRange).has_value());  // the midpoint
    CHECK_FALSE(candidateWithinGuard({}, 1.0e-3, kRange).has_value());
}

TEST_CASE("guard: ONE candidate is bounded by the search range, not accepted at any distance", "[cycle_arrival]") {
    // H5 closeout D6. A single maximum c in a search of +-R means the missing
    // neighbours lie outside it: spacing > R + |c|, so the quarter rule gives
    // d <= (R + |c|) / 4. R = 20 ms, c = 1 ms -> 5.25 ms.
    constexpr double kRange = 20.0e-3;
    constexpr double kCandidate = 1.0e-3;
    const double bound = (kRange + kCandidate) / 4.0;
    CHECK(candidateWithinGuard({ kCandidate }, kCandidate + 2.0e-3, kRange) == std::optional<double>(kCandidate));
    CHECK(candidateWithinGuard({ kCandidate }, kCandidate + bound - 1e-6, kRange).has_value());
    CHECK_FALSE(candidateWithinGuard({ kCandidate }, kCandidate + bound + 1e-6, kRange).has_value());
    CHECK_FALSE(candidateWithinGuard({ kCandidate }, kCandidate - bound - 1e-6, kRange).has_value());
    // The old behaviour: an integer 49 ms from the only candidate was accepted.
    CHECK_FALSE(candidateWithinGuard({ kCandidate }, 50.0e-3, kRange).has_value());
    // No range, no bound: ask.
    CHECK_FALSE(candidateWithinGuard({ kCandidate }, kCandidate, 0.0).has_value());
}

TEST_CASE("seed vs crossing: a third of an octave is the line", "[cycle_arrival]") {
    using rta::measure::seedAgreesWithCrossing;
    // H5 closeout D9. Inside, both directions.
    CHECK(seedAgreesWithCrossing(100.0, 100.0));
    CHECK(seedAgreesWithCrossing(100.0 * std::pow(2.0, 0.30), 100.0));
    CHECK(seedAgreesWithCrossing(100.0 * std::pow(2.0, -0.30), 100.0));
    CHECK_FALSE(seedAgreesWithCrossing(100.0 * std::pow(2.0, 0.36), 100.0));
    CHECK_FALSE(seedAgreesWithCrossing(100.0 * std::pow(2.0, -0.36), 100.0));
    // The PR #72 verifier's wrong-cycle cases: SEED 60 on a 200 Hz LR4 corner
    // (residual 5.4 ms, inside [3s/4, 5s/4]); for LR8 one notch off sufficed:
    // SEED 80 on 150 Hz, SEED 100 on 200 Hz, SEED 60 on 120 Hz.
    CHECK_FALSE(seedAgreesWithCrossing(60.0, 200.0));
    CHECK_FALSE(seedAgreesWithCrossing(80.0, 150.0));
    CHECK_FALSE(seedAgreesWithCrossing(100.0, 200.0));
    CHECK_FALSE(seedAgreesWithCrossing(60.0, 120.0));
    // Nothing to compare is not a disagreement.
    CHECK(seedAgreesWithCrossing(0.0, 100.0));
    CHECK(seedAgreesWithCrossing(100.0, 0.0));
}

TEST_CASE("seed vs crossing: 1/3 octave keeps the LR8 lag error under the quarter-spacing guard", "[cycle_arrival]") {
    // The derivation in CycleArrival.h, checked against the real lags: at a true
    // corner f_t, a seed at f_t * 2^(+-1/3) mis-corrects the LR8 low-pass lag by
    // less than a quarter of the ~1/f_t candidate spacing; one octave off does not.
    constexpr double kTrue = 150.0;
    const double lagTrue = lowPassLag(CrossoverFamily::LinkwitzRiley, 8, kTrue);
    const double quarterSpacingSamples = kFs / kTrue / 4.0;
    for (const double octaves : { -1.0 / 3.0, 1.0 / 3.0 }) {
        const double lagSeed = lowPassLag(CrossoverFamily::LinkwitzRiley, 8, kTrue * std::pow(2.0, octaves));
        CHECK(std::fabs(lagSeed - lagTrue) <= quarterSpacingSamples * 1.1);  // +10 %: the 1.08/fc spacing and rounding
    }
    const double lagFar = lowPassLag(CrossoverFamily::LinkwitzRiley, 8, kTrue * 0.5);
    CHECK(std::fabs(lagFar - lagTrue) > quarterSpacingSamples);
}
