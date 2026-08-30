// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/ir/Decay.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <random>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double kFs = 48000.0;

/// Lead-in placed before the arrival in every fixture here.
///
/// Not decoration. A zero-phase filter is non-causal; with the arrival at index
/// 0 its padding fabricates 38.7 dB of energy exactly where EDT reads. The
/// fixtures carry the lead-in for the same reason the shipped code refuses
/// without one.
constexpr std::size_t kLeadIn = 9600;   // 200 ms

/// Band-limited decay with a T60 that is exact by construction.
///
/// Energy of `n(t)*exp(-t/tau)` falls as `exp(-2t/tau)`, so a 60 dB drop takes
/// `T60 = 3*ln(10)*tau`. Inverting that is the only place the expected answer
/// comes from -- nothing here is a figure the implementation printed.
///
/// `snrDb` is measured against the fixture's OWN direct sound, never an
/// absolute amplitude: an absolute noise line makes the SNR depend on whatever
/// peak the signal happens to have, and 6 dB of SNR is enough to move the
/// truncation point and change T30 -- or change whether there is an answer.
[[nodiscard]] std::vector<float> makeDecay(double t60Sec, double seconds,
                                           double snrDb, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> gauss(0.0, 1.0);

    const auto tailLen = static_cast<std::size_t>(seconds * kFs);
    std::vector<float> h(kLeadIn + tailLen, 0.0f);

    const double tau = t60Sec / (3.0 * std::log(10.0));
    for (std::size_t i = 0; i < tailLen; ++i) {
        const double t = static_cast<double>(i) / kFs;
        h[kLeadIn + i] = static_cast<float>(gauss(rng) * std::exp(-t / tau));
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(0.006 * kFs); ++i) {
        h[kLeadIn + i] = 0.0f;
    }
    h[kLeadIn] = 1.0f;

    const double noiseRms = std::pow(10.0, -snrDb / 20.0);
    for (auto& v : h) v += static_cast<float>(gauss(rng) * noiseRms);
    return h;
}

}  // namespace

TEST_CASE("A pure exponential decay reads back the T60 it was built with", "[ir][decay]") {
    // 800 Hz octave: B = 566 Hz, so B*T is 226 at T60 = 0.4 s -- far above the
    // gate, which is the point. This case tests the arithmetic, not the gate.
    const auto h = makeDecay(0.4, 1.6, 60.0, 1234);
    const auto band = rta::ir::bandFilterZeroPhase(h, 566.0, 1132.0, kFs);
    REQUIRE(band.size() == h.size());

    const auto curve = rta::ir::energyDecayCurve(band, kLeadIn, kFs, 566.0);
    REQUIRE(curve.has());
    CHECK(curve.db.front() == Catch::Approx(0.0).margin(1e-6));

    const auto times = rta::ir::decayTimes(curve);
    REQUIRE(times.t30.has());
    REQUIRE(times.t20.has());

    // 10 % is the envelope this lane measured for a single realisation in a
    // wide band, not a number chosen to make the test pass: the ensemble median
    // sits within a few percent and the inter-quartile spread at this bandwidth
    // is about 3-5 %.
    CHECK_THAT(times.t30.seconds, WithinRel(0.4, 0.10));
    CHECK_THAT(times.t20.seconds, WithinRel(0.4, 0.10));
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

TEST_CASE("Clarity of a two-burst fixture is derivable in closed form",
          "[ir][decay]") {
    // Two equal bursts, one inside the 50 ms window and one outside it, give
    // C50 = 0 dB and D50 = 0.5 EXACTLY -- arithmetic, not a printed value.
    //
    // The curve is hand-built rather than measured so the crossing sits past
    // both bursts and the truncation correction is zero: this case is about the
    // ratio arithmetic, and a fixture that also exercised Lundeby could not say
    // which of the two was wrong when it failed.
    std::vector<float> h(static_cast<std::size_t>(0.3 * kFs), 0.0f);
    h[0] = 1.0f;
    h[static_cast<std::size_t>(0.060 * kFs)] = 1.0f;

    rta::ir::EnergyDecayCurve curve;
    curve.sampleRate = kFs;
    curve.crossingIndex = h.size();
    curve.truncationCorrection = 0.0;
    curve.refusal = rta::ir::DecayRefusal::None;

    const auto c = rta::ir::clarity(h, 0, kFs, curve);
    REQUIRE(c.c50Db.has());
    REQUIRE(c.d50.has());
    CHECK_THAT(c.c50Db.value, WithinAbs(0.0, 1e-9));
    CHECK_THAT(c.d50.value, WithinAbs(0.5, 1e-9));

    // Both bursts fall inside 80 ms, so C80 has no late half at all and must
    // say so. Crucially it must do that WITHOUT taking C50 down with it: the
    // 50 ms answer is fully determined here, and a single verdict for all three
    // would discard it.
    CHECK_FALSE(c.c80Db.has());
    CHECK(c.c80Db.refusal == rta::ir::DecayRefusal::RangeTooSmall);
}

TEST_CASE("Clarity does not depend on how long the recorder ran", "[ir][decay]") {
    // The defect this signature exists to prevent, asserted rather than
    // described. The late term is an integral to the end of the record, so
    // untruncated it grows with every noise-only sample and C50 falls as a
    // function of a decision the operator made for unrelated reasons.
    //
    // Being a ratio does not save it: the numerator is bounded by the 50 ms
    // split and only the denominator grows.
    const double t60 = 0.4;
    const auto shortTail = makeDecay(t60, 2.5 * t60, 50.0, 4242);
    const auto longTail = makeDecay(t60, 7.5 * t60, 50.0, 4242);

    const auto a = rta::ir::bandFilterZeroPhase(shortTail, 566.0, 1132.0, kFs);
    const auto b = rta::ir::bandFilterZeroPhase(longTail, 566.0, 1132.0, kFs);
    const auto ca = rta::ir::energyDecayCurve(a, kLeadIn, kFs, 566.0);
    const auto cb = rta::ir::energyDecayCurve(b, kLeadIn, kFs, 566.0);
    REQUIRE(ca.has());
    REQUIRE(cb.has());

    const auto x = rta::ir::clarity(a, kLeadIn, kFs, ca);
    const auto y = rta::ir::clarity(b, kLeadIn, kFs, cb);
    REQUIRE(x.c50Db.has());
    REQUIRE(y.c50Db.has());
    CHECK_THAT(x.c50Db.value, WithinAbs(y.c50Db.value, 1.5));
    CHECK_THAT(x.d50.value, WithinAbs(y.d50.value, 0.05));
}

TEST_CASE("Clarity inherits the curve's refusal rather than inventing one",
          "[ir][decay]") {
    // Without a crossing there is no honest end to the late integral. Returning
    // a number anyway is exactly the untruncated behaviour above; returning a
    // DIFFERENT reason would send the operator to the wrong diagnosis.
    std::vector<float> h(static_cast<std::size_t>(0.3 * kFs), 0.0f);
    h[0] = 1.0f;

    rta::ir::EnergyDecayCurve refused;
    refused.sampleRate = kFs;
    refused.refusal = rta::ir::DecayRefusal::BandwidthTimeTooSmall;

    const auto c = rta::ir::clarity(h, 0, kFs, refused);
    CHECK_FALSE(c.any());
    CHECK(c.c50Db.refusal == rta::ir::DecayRefusal::BandwidthTimeTooSmall);
    CHECK(c.c80Db.refusal == rta::ir::DecayRefusal::BandwidthTimeTooSmall);
    CHECK(c.d50.refusal == rta::ir::DecayRefusal::BandwidthTimeTooSmall);
}

TEST_CASE("A capture that ends before the split point refuses", "[ir][decay]") {
    // 60 ms of response cannot answer C80. Treating the missing late half as
    // silence would report enormous clarity -- the reading an operator is least
    // equipped to disbelieve, because it looks like a very good room.
    std::vector<float> h(static_cast<std::size_t>(0.060 * kFs), 0.0f);
    h[0] = 1.0f;

    rta::ir::EnergyDecayCurve curve;
    curve.sampleRate = kFs;
    curve.crossingIndex = h.size();
    curve.refusal = rta::ir::DecayRefusal::None;

    const auto c = rta::ir::clarity(h, 0, kFs, curve);
    CHECK_FALSE(c.any());
    CHECK(c.c50Db.refusal == rta::ir::DecayRefusal::RangeTooSmall);
}

TEST_CASE("An impossible band is refused at the door", "[ir][decay]") {
    std::vector<float> h(1000, 0.0f);
    CHECK_THROWS_AS(rta::ir::bandFilterZeroPhase(h, 0.0, 100.0, kFs),
                    std::invalid_argument);
    CHECK_THROWS_AS(rta::ir::bandFilterZeroPhase(h, 200.0, 100.0, kFs),
                    std::invalid_argument);
    CHECK_THROWS_AS(rta::ir::bandFilterZeroPhase(h, 100.0, 40000.0, kFs),
                    std::invalid_argument);
    CHECK_THROWS_AS(rta::ir::bandFilterZeroPhase(h, 100.0, 200.0, 0.0),
                    std::invalid_argument);
}

TEST_CASE("Energy the filter moved before the arrival counts as early",
          "[ir][decay]") {
    // Zero-phase filtering is symmetric about an impulse, so half the direct
    // sound's band energy lands BEFORE the arrival. It is that sound's own
    // energy, moved by a filter that conserves it -- not leakage. Dropping it
    // is what creates a filter-dependent loss: measured against the C50 of the
    // unfiltered response, excluding costs -3.06 dB at 1/3-octave 40 Hz where
    // including costs -0.33 dB.
    //
    // This asserts the convention arithmetically rather than by re-deriving the
    // survey: the two candidate numerators are both computable here, and the
    // implementation must equal the one that keeps the pre-arrival energy.
    std::vector<float> h(static_cast<std::size_t>(0.5 * kFs), 0.0f);
    h[kLeadIn] = 1.0f;
    h[kLeadIn + static_cast<std::size_t>(0.200 * kFs)] = 1.0f;

    const double lo = 63.0 / std::pow(2.0, 1.0 / 6.0);
    const double hi = 63.0 * std::pow(2.0, 1.0 / 6.0);
    const auto band = rta::ir::bandFilterZeroPhase(h, lo, hi, kFs);

    rta::ir::EnergyDecayCurve curve;
    curve.sampleRate = kFs;
    curve.crossingIndex = band.size() - kLeadIn;
    curve.truncationCorrection = 0.0;
    curve.refusal = rta::ir::DecayRefusal::None;

    double preOrigin = 0.0, early = 0.0, total = 0.0;
    const auto at50 = static_cast<std::size_t>(0.050 * kFs);
    for (std::size_t i = 0; i < band.size(); ++i) {
        const double e = static_cast<double>(band[i]) * static_cast<double>(band[i]);
        total += e;
        if (i < kLeadIn) preOrigin += e;
        else if (i - kLeadIn < at50) early += e;
    }

    // The filter really does put a substantial share before the arrival --
    // otherwise this test would pass for both conventions and prove nothing.
    REQUIRE(preOrigin > 0.05 * total);

    const auto c = rta::ir::clarity(band, kLeadIn, kFs, curve);
    REQUIRE(c.d50.has());
    CHECK_THAT(c.d50.value, WithinRel((preOrigin + early) / total, 1e-6));
    CHECK(c.d50.value > early / total);   // i.e. NOT the excluding convention
}
