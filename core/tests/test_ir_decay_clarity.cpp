// SPDX-License-Identifier: AGPL-3.0-or-later
// Split out of test_ir_decay.cpp (process-tooling PR, 400-line file cap):
// clarity (C50/C80/D50) and the multi-capture decay-time ensemble. Decay-time
// arithmetic, the bandwidth-time gate and zero-phase filtering stay in
// test_ir_decay.cpp; both files share the [ir][decay] Catch2 tags and this
// one rta_core_tests target. See support/DecayFixtures.h for the shared
// makeDecay/kLeadIn/kFs fixture.
#include "rta/ir/Decay.h"
#include "rta/ir/DecayEnsemble.h"
#include "rta/ir/IrSpectrum.h"
#include "support/DecayFixtures.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using namespace rta::test::decay_fixtures;

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

    const auto c = rta::ir::clarity(h, 0, kFs, 400.0, curve);
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

    const auto x = rta::ir::clarity(a, kLeadIn, kFs, 566.0, ca);
    const auto y = rta::ir::clarity(b, kLeadIn, kFs, 566.0, cb);
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

    const auto c = rta::ir::clarity(h, 0, kFs, 400.0, refused);
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

    const auto c = rta::ir::clarity(h, 0, kFs, 400.0, curve);
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

    // Only the filter's own pre-ring -- two cycles of the low edge before the
    // origin -- is counted; the rest of the lead-in is outside the bound.
    const std::size_t preRing = rta::ir::leadInSamplesFor(kFs, lo);
    REQUIRE(preRing < kLeadIn);                                   // premise
    double preOrigin = 0.0, early = 0.0, total = 0.0, dropped = 0.0;
    const auto at50 = static_cast<std::size_t>(0.050 * kFs);
    for (std::size_t i = 0; i < band.size(); ++i) {
        const double e = static_cast<double>(band[i]) * static_cast<double>(band[i]);
        if (i < kLeadIn - preRing) { dropped += e; continue; }
        total += e;
        if (i < kLeadIn) preOrigin += e;
        else if (i - kLeadIn < at50) early += e;
    }
    // The bound is not free, and this is its measured price (regression lock,
    // not a derivation): at a 1/3-octave 63 Hz band the energy beyond two cycles
    // of the low edge is 3.4 % of the response's total, so D50 here moves by at
    // most 0.034 (the dropped share bounds the shift of a 0..1 ratio).
    CHECK(dropped < 0.05 * (dropped + total));

    // The filter really does put a substantial share before the arrival --
    // otherwise this test would pass for both conventions and prove nothing.
    REQUIRE(preOrigin > 0.05 * total);

    const auto c = rta::ir::clarity(band, kLeadIn, kFs, lo, curve);
    REQUIRE(c.d50.has());
    CHECK_THAT(c.d50.value, WithinRel((preOrigin + early) / total, 1e-6));
    CHECK(c.d50.value > early / total);   // i.e. NOT the excluding convention
}

TEST_CASE("Several captures report what they agree on and how much they do not",
          "[ir][decay]") {
    // The shipped answer to EDT's variance. A per-reading confidence score was
    // measured and rejected -- neither the fit residual nor the curvature
    // correlates with the actual error (|corr| 0.02 to 0.16 over 300
    // realisations) -- so the only honest figure is the spread across captures
    // actually taken. Averaging shrinks it as 1/sqrt(N), measured.
    std::vector<rta::ir::EnergyDecayCurve> curves;
    for (unsigned seed : {11u, 22u, 33u, 44u, 55u}) {
        const auto h = makeDecay(0.6, 2.4, 55.0, seed);
        const auto band = rta::ir::bandFilterZeroPhase(h, 566.0, 1132.0, kFs);
        auto curve = rta::ir::energyDecayCurve(band, kLeadIn, kFs, 566.0);
        REQUIRE(curve.has());
        curves.push_back(std::move(curve));
    }

    const auto across = rta::ir::decayTimesAcross(curves);
    REQUIRE(across.t30.value.has());
    CHECK(across.t30.captures == 5);
    CHECK(across.t30.spreadIsMeaningful());
    CHECK_THAT(across.t30.value.seconds, WithinRel(0.6, 0.10));

    // A spread of exactly zero across five independent captures would mean the
    // caller passed the same capture five times -- the failure this type exists
    // to make visible rather than to hide.
    CHECK(across.t30.spreadPercent > 0.0);
}

TEST_CASE("Two captures report a value but refuse to call it a spread",
          "[ir][decay]") {
    // With two points an inter-quartile range is the gap between them. Reporting
    // that as a spread would read as precision while carrying none, which is the
    // same false reassurance the rejected per-reading score would have given.
    std::vector<rta::ir::EnergyDecayCurve> curves;
    for (unsigned seed : {11u, 22u}) {
        const auto h = makeDecay(0.6, 2.4, 55.0, seed);
        const auto band = rta::ir::bandFilterZeroPhase(h, 566.0, 1132.0, kFs);
        curves.push_back(rta::ir::energyDecayCurve(band, kLeadIn, kFs, 566.0));
    }

    const auto across = rta::ir::decayTimesAcross(curves);
    REQUIRE(across.t30.value.has());
    CHECK(across.t30.captures == 2);
    CHECK_FALSE(across.t30.spreadIsMeaningful());
    CHECK(across.t30.spreadPercent == 0.0);
}

TEST_CASE("When every capture refuses, the reason survives the aggregation",
          "[ir][decay]") {
    // A caller shown "no decay found" when the real answer was "this band is
    // too narrow for this room" goes looking in the wrong place. The first
    // reason is carried through rather than replaced by a generic one.
    //
    // The curves are constructed rather than measured, deliberately. An earlier
    // version of this test built them from a 40 Hz third-octave against a 0.4 s
    // room and REQUIREd each to refuse -- and it went red, because that cell
    // sits at a measured B*T of about 5.3 to 5.7 against a threshold of 6.0 and
    // some realisations land above. That margin of 0.29 is documented in the
    // header as thin; a test of the AGGREGATION should not also be a bet on
    // which side of it a seed falls.
    rta::ir::EnergyDecayCurve refused;
    refused.sampleRate = kFs;
    refused.refusal = rta::ir::DecayRefusal::BandwidthTimeTooSmall;
    const std::vector<rta::ir::EnergyDecayCurve> curves(3, refused);

    const auto across = rta::ir::decayTimesAcross(curves);
    CHECK_FALSE(across.t30.value.has());
    CHECK(across.t30.captures == 0);
    CHECK(across.t30.value.refusal == rta::ir::DecayRefusal::BandwidthTimeTooSmall);
    CHECK_FALSE(across.edt.value.has());
    CHECK(across.edt.value.refusal == rta::ir::DecayRefusal::BandwidthTimeTooSmall);
}

TEST_CASE("Clarity counts the pre-ring but not the sweep-length pre-arrival region",
          "[ir][decay]") {
    // On a real Deconvolution `originIndex` is a whole sweep length in (here
    // 2 s), and the region before it holds pre-arrival floor and harmonic
    // packets. Only the last leadInSamplesFor(fs, lowHz) samples before the
    // arrival are the filter's own pre-ring.
    //
    // Hand-built response, closed form. 400 Hz low edge -> 2*48000/400 = 240
    // pre-ring samples, so the counted window is [arrival-240, arrival).
    //   unit packet at the arrival and another at +60 ms
    //   a unit packet INSIDE the bound (arrival-240, its first sample)
    //   big packets OUTSIDE it (arrival-241, and far back at index 1000)
    // Counted: early50 = 1 (arrival) + 1 (pre-ring) = 2, late50 = 1, total = 3:
    //   C50 = 10 log10(2),  D50 = 2/3  -- the outside packets must not appear.
    const std::size_t arrival = static_cast<std::size_t>(2.0 * kFs);
    std::vector<float> h(arrival + static_cast<std::size_t>(0.3 * kFs), 0.0f);
    h[arrival] = 1.0f;
    h[arrival + static_cast<std::size_t>(0.060 * kFs)] = 1.0f;
    h[arrival - 240] = 1.0f;
    h[arrival - 241] = 5.0f;
    h[1000] = 5.0f;

    rta::ir::EnergyDecayCurve curve;
    curve.sampleRate = kFs;
    curve.crossingIndex = h.size() - arrival;
    curve.truncationCorrection = 0.0;
    curve.refusal = rta::ir::DecayRefusal::None;

    const auto c = rta::ir::clarity(h, arrival, kFs, 400.0, curve);
    REQUIRE(c.c50Db.has());
    REQUIRE(c.d50.has());
    CHECK_THAT(c.c50Db.value, WithinAbs(10.0 * std::log10(2.0), 1e-9));
    CHECK_THAT(c.d50.value, WithinAbs(2.0 / 3.0, 1e-9));

    // An arrival nearer the start than the bound uses what there is.
    std::vector<float> g(static_cast<std::size_t>(0.3 * kFs), 0.0f);
    g[100] = 1.0f;
    g[100 + static_cast<std::size_t>(0.060 * kFs)] = 1.0f;
    g[40] = 1.0f;                                         // 60 samples before it
    rta::ir::EnergyDecayCurve gc = curve;
    gc.crossingIndex = g.size() - 100;
    const auto d = rta::ir::clarity(g, 100, kFs, 400.0, gc);
    REQUIRE(d.d50.has());
    CHECK_THAT(d.d50.value, WithinAbs(2.0 / 3.0, 1e-9));

    // A band with no low edge cannot size the bound: refuse, never guess.
    CHECK_FALSE(rta::ir::clarity(h, arrival, kFs, 0.0, curve).any());
}
