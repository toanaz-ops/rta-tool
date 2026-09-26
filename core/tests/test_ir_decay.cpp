// SPDX-License-Identifier: AGPL-3.0-or-later
// Split (process-tooling PR, 400-line file cap): this file covers T60/T20/T30
// decay-time arithmetic, the bandwidth-time gate, and zero-phase filtering.
// Clarity (C50/C80/D50) and the multi-capture ensemble live in
// test_ir_decay_clarity.cpp; the makeDecay/kLeadIn/kFs fixture both files
// need is shared via support/DecayFixtures.h so it cannot drift between them.
#include "rta/ir/Decay.h"
#include "rta/ir/DecayEnsemble.h"
#include "support/DecayFixtures.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using namespace rta::test::decay_fixtures;

TEST_CASE("A pure exponential decay reads back the T60 it was built with", "[ir][decay]") {
    // 800 Hz octave: B = 566 Hz, so B*T is 226 at T60 = 0.4 s -- far above the
    // gate, which is the point. This case tests the arithmetic, not the gate.
    //
    // FIVE seeds, asserted on the MEDIAN. An earlier version ran one seed
    // against the +-10 % envelope this lane measured for an ENSEMBLE, which is
    // the mistake of dressing a single realisation in a population's tolerance:
    // it passes or fails on which noise the generator happened to produce, and
    // a version of the code that was genuinely 8 % out could sit green for as
    // long as the seed was kind. The median of five is not an ensemble either,
    // but it cannot be carried by one lucky draw.
    std::vector<double> t30s, t20s;
    for (unsigned seed : {1234u, 1235u, 1236u, 1237u, 1238u}) {
        const auto h = makeDecay(0.4, 1.6, 60.0, seed);
        const auto band = rta::ir::bandFilterZeroPhase(h, 566.0, 1132.0, kFs);
        REQUIRE(band.size() == h.size());

        const auto curve = rta::ir::energyDecayCurve(band, kLeadIn, kFs, 566.0);
        REQUIRE(curve.has());
        CHECK(curve.db.front() == Catch::Approx(0.0).margin(1e-6));

        const auto times = rta::ir::decayTimes(curve);
        REQUIRE(times.t30.has());
        REQUIRE(times.t20.has());
        t30s.push_back(times.t30.seconds);
        t20s.push_back(times.t20.seconds);
    }

    const auto median = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    CHECK_THAT(median(t30s), WithinRel(0.4, 0.10));
    CHECK_THAT(median(t20s), WithinRel(0.4, 0.10));
}

TEST_CASE("The decay time is -60/slope, not -60/slope times a span factor",
          "[ir][decay]") {
    // The x2 / x3 / x6 multiplier that reference implementations apply is a
    // double count here. If it crept in, T20 and T30 would disagree with each
    // other by the ratio of their factors (3/2) on a signal that has exactly
    // one decay rate. That disagreement is what this asserts against, and it is
    // a stronger check than comparing either one to 0.4 alone.
    const auto h = makeDecay(0.6, 2.4, 60.0, 99);
    const auto band = rta::ir::bandFilterZeroPhase(h, 566.0, 1132.0, kFs);
    const auto curve = rta::ir::energyDecayCurve(band, kLeadIn, kFs, 566.0);
    REQUIRE(curve.has());

    const auto times = rta::ir::decayTimes(curve);
    REQUIRE(times.t20.has());
    REQUIRE(times.t30.has());
    CHECK_THAT(times.t20.seconds, WithinRel(times.t30.seconds, 0.15));
}

TEST_CASE("A band too narrow for the decay refuses instead of measuring itself",
          "[ir][decay]") {
    // 1/3-octave at 40 Hz is B = 9.3 Hz. Against a 0.4 s decay that is
    // B*T = 3.7, below the floor of about 4 that this lane measured -- at which
    // point no filtering mode is usable and what would be reported is largely
    // the filter's own decay. Record decision 4c.
    const auto h = makeDecay(0.4, 1.6, 60.0, 7);
    const double lo = 40.0 / std::pow(2.0, 1.0 / 6.0);
    const double hi = 40.0 * std::pow(2.0, 1.0 / 6.0);
    const auto band = rta::ir::bandFilterZeroPhase(h, lo, hi, kFs);

    const auto curve = rta::ir::energyDecayCurve(band, kLeadIn, kFs, hi - lo);
    CAPTURE(curve.lateDecaySec, (hi - lo) * curve.lateDecaySec);
    CHECK_FALSE(curve.has());
    CHECK(curve.refusal == rta::ir::DecayRefusal::BandwidthTimeTooSmall);
}

TEST_CASE("The same band passes the gate once the room is slow enough",
          "[ir][decay]") {
    // The gate is not a property of the band alone. The same 40 Hz third-octave
    // against a 1.2 s decay is B*T = 11.1, comfortably above the floor -- so a
    // test that only ever saw the refusal could not tell a working gate from
    // one wired shut.
    const auto h = makeDecay(1.2, 4.8, 60.0, 7);
    const double lo = 40.0 / std::pow(2.0, 1.0 / 6.0);
    const double hi = 40.0 * std::pow(2.0, 1.0 / 6.0);
    const auto band = rta::ir::bandFilterZeroPhase(h, lo, hi, kFs);

    const auto curve = rta::ir::energyDecayCurve(band, kLeadIn, kFs, hi - lo);
    CHECK(curve.refusal != rta::ir::DecayRefusal::BandwidthTimeTooSmall);
}

TEST_CASE("Passing no bandwidth skips the gate rather than failing it",
          "[ir][decay]") {
    // A broadband curve has no bandwidth to gate on. Zero must mean "do not
    // gate", not "gate against zero" -- the latter would refuse everything, and
    // would do it in a way that looks like a real refusal.
    const auto h = makeDecay(0.4, 1.6, 60.0, 7);
    const double lo = 40.0 / std::pow(2.0, 1.0 / 6.0);
    const double hi = 40.0 * std::pow(2.0, 1.0 / 6.0);
    const auto band = rta::ir::bandFilterZeroPhase(h, lo, hi, kFs);

    const auto curve = rta::ir::energyDecayCurve(band, kLeadIn, kFs, 0.0);
    CHECK(curve.refusal != rta::ir::DecayRefusal::BandwidthTimeTooSmall);
}

TEST_CASE("A refusal and a missing number always agree", "[ir][decay]") {
    // `has()` and `refusal` carry the same fact. Asserting the equivalence in
    // BOTH directions is what stops one of them drifting into meaning something
    // the other does not -- the same discipline Polarity uses.
    const rta::ir::DecayTime absent{};
    CHECK_FALSE(absent.has());
    CHECK(absent.refusal != rta::ir::DecayRefusal::None);

    rta::ir::DecayTime present;
    present.seconds = 0.5;
    present.refusal = rta::ir::DecayRefusal::None;
    CHECK(present.has());
}

TEST_CASE("Truncation makes the answer independent of how long the tail is",
          "[ir][decay]") {
    // The central claim of this lane, asserted rather than described. The same
    // room, recorded for 2.5x and 7.5x its own T60, must give the same T30.
    //
    // Untruncated, the second reads roughly ten times the first: the backward
    // integral accumulates every noise sample into its plateau, so the answer
    // depends on when the operator stopped recording. If truncation regressed,
    // this test separates -- and it separates by a factor, not by a few percent,
    // which is why it is worth its runtime.
    const double t60 = 0.4;
    const auto shortTail = makeDecay(t60, 2.5 * t60, 50.0, 2026);
    const auto longTail = makeDecay(t60, 7.5 * t60, 50.0, 2026);

    const auto a = rta::ir::bandFilterZeroPhase(shortTail, 566.0, 1132.0, kFs);
    const auto b = rta::ir::bandFilterZeroPhase(longTail, 566.0, 1132.0, kFs);

    const auto ca = rta::ir::energyDecayCurve(a, kLeadIn, kFs, 566.0);
    const auto cb = rta::ir::energyDecayCurve(b, kLeadIn, kFs, 566.0);
    REQUIRE(ca.has());
    REQUIRE(cb.has());

    const auto ta = rta::ir::decayTimes(ca).t30;
    const auto tb = rta::ir::decayTimes(cb).t30;
    REQUIRE(ta.has());
    REQUIRE(tb.has());
    CHECK_THAT(ta.seconds, WithinRel(tb.seconds, 0.20));
}

TEST_CASE("Zero-phase filtering leaves the arrival where it was", "[ir][decay]") {
    // The property that makes it zero-phase, asserted directly rather than
    // assumed from the name. A forward-only cascade delays the peak by its own
    // group delay; forward-backward cancels it. If the second pass were dropped
    // or the reversal mishandled, the peak would move and every early/late
    // split downstream -- C50, C80, D50, EDT -- would move with it.
    std::vector<float> spike(4800, 0.0f);
    spike[2400] = 1.0f;

    const auto band = rta::ir::bandFilterZeroPhase(spike, 707.0, 1414.0, kFs);
    REQUIRE(band.size() == spike.size());

    std::size_t peak = 0;
    for (std::size_t i = 1; i < band.size(); ++i) {
        if (std::abs(band[i]) > std::abs(band[peak])) peak = i;
    }
    CHECK(peak >= 2395);
    CHECK(peak <= 2405);
}
