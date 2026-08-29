// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests. Decision 3 of
// docs/dsp/2026-08-29-display-layer-l5c.md, continuous half only. Every
// THRESHOLD -- blanking, gating, a 0.95-style default -- is L5b's, and no test
// in this file may assert one.
#include "view/CoherenceAlpha.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

TEST_CASE("an untrusted region dims but never vanishes", "[coherence-alpha]") {
    // The floor is what separates a FADE from a DELETION. Deleting data
    // quietly is the gate's job, and the gate is required to do it loudly --
    // which is L5b's decision to make, not this file's.
    CHECK(rta::view::alphaForCoherence(0.0f)
          == Catch::Approx(rta::view::kUntrustedAlphaFloor));
    CHECK(rta::view::kUntrustedAlphaFloor > 0.0f);
    CHECK(rta::view::alphaForCoherence(1.0f) == Catch::Approx(1.0f));
}

TEST_CASE("alpha is monotone in coherence", "[coherence-alpha]") {
    // The one property the record actually fixes: "a monotone function of
    // gamma^2 with a floor of 0.25". The particular curve is a judgement; that
    // it never decreases as trust rises is not.
    float previous = -1.0f;
    for (int i = 0; i <= 100; ++i) {
        const float gamma = static_cast<float>(i) / 100.0f;
        const float alpha = rta::view::alphaForCoherence(gamma);
        CHECK(alpha >= previous);
        CHECK(alpha >= rta::view::kUntrustedAlphaFloor);
        CHECK(alpha <= 1.0f);
        previous = alpha;
    }

    // Linearity pinned by an interior point, because the sweep above only
    // forbids a DIP. A monotone STEP at 0.95 -- a threshold wearing a fade's
    // clothes -- passes every other assertion in this file, and thresholds
    // belong to L5b. If that lane ever wants a different curve it must edit
    // this line too, which is the point: the shape becomes a deliberate change
    // rather than a drift.
    CHECK(rta::view::alphaForCoherence(0.5f) == Catch::Approx(0.625f));
}

TEST_CASE("nonsense trust is treated as no trust", "[coherence-alpha]") {
    // core clamps coherence to [0,1] and withholds it entirely below the gate,
    // so these cannot arrive from the live engine -- but a stored trace comes
    // off disk, and a NaN that compares false against every bound would sail
    // through an unguarded comparison and paint at FULL confidence.
    CHECK(rta::view::alphaForCoherence(std::numeric_limits<float>::quiet_NaN())
          == Catch::Approx(rta::view::kUntrustedAlphaFloor));
    CHECK(rta::view::alphaForCoherence(-0.5f)
          == Catch::Approx(rta::view::kUntrustedAlphaFloor));
    CHECK(rta::view::alphaForCoherence(4.0f) == Catch::Approx(1.0f));
}

TEST_CASE("a column takes the MINIMUM trust of its bins", "[coherence-alpha]") {
    // Decision 3: trust shown never exceeds trust measured. A mean would let
    // one confident bin carry three noisy ones into looking solid, at exactly
    // the frequencies -- a null, the LF end -- where the operator is deciding
    // whether to move a delay.
    const std::vector<float> coherence{ 1.0f, 0.2f, 1.0f, 0.9f, 0.8f, 0.95f };
    const std::vector<int> columnForBin{ 0, 0, 0, 1, 1, 1 };

    const auto alpha = rta::view::columnAlpha(coherence, columnForBin, 2);
    REQUIRE(alpha.size() == 2u);
    CHECK(alpha[0] == Catch::Approx(rta::view::alphaForCoherence(0.2f)));
    CHECK(alpha[1] == Catch::Approx(rta::view::alphaForCoherence(0.8f)));

    // And it is genuinely the minimum, not the first or the last: reversing
    // each column's contents must not change the answer.
    const std::vector<float> reversed{ 1.0f, 1.0f, 0.2f, 0.95f, 0.8f, 0.9f };
    const auto again = rta::view::columnAlpha(reversed, columnForBin, 2);
    CHECK(again[0] == Catch::Approx(alpha[0]));
    CHECK(again[1] == Catch::Approx(alpha[1]));
}

TEST_CASE("an unmeasurable bin cannot be hidden by its neighbours",
          "[coherence-alpha]") {
    // The scalar guard in alphaForCoherence is not enough on its own, and this
    // is the case that proves it. decimateToColumns' min accumulation asks
    // `v < extent.minValue`, which is FALSE for NaN, so a NaN arriving after a
    // good bin is dropped and the column reports the good bin's trust.
    //
    // Both orders must floor. If only one does, the rendered trust depends on
    // which end of a column a corrupted sample happens to sit at -- and the
    // order that paints full confidence is the one that puts an unmeasurable
    // reading on screen looking like a solid measurement.
    const float notMeasured = std::numeric_limits<float>::quiet_NaN();
    const std::vector<int> columnForBin{ 0, 0 };

    const auto nanFirst =
        rta::view::columnAlpha(std::vector<float>{ notMeasured, 1.0f }, columnForBin, 1);
    const auto nanLast =
        rta::view::columnAlpha(std::vector<float>{ 1.0f, notMeasured }, columnForBin, 1);

    REQUIRE(nanFirst.size() == 1u);
    REQUIRE(nanLast.size() == 1u);
    CHECK(nanFirst[0] == Catch::Approx(rta::view::kUntrustedAlphaFloor));
    CHECK(nanLast[0] == Catch::Approx(rta::view::kUntrustedAlphaFloor));
}

// Record §5a: bridging narrowed this property to the ends of the axis. It
// used to hold everywhere a bin was missing; now it holds only where there is
// no measured column on one side to interpolate from at all.
TEST_CASE("a column with no bins at either end of the axis reports no trust",
          "[coherence-alpha]") {
    // Bins land only in the MIDDLE column (1); columns 0 and 2 have nothing
    // on their outward side to bridge from -- a leading and a trailing gap
    // in the same three-column axis.
    const std::vector<float> coherence{ 0.9f, 0.9f };
    const std::vector<int> columnForBin{ 1, 1 };
    const auto alpha = rta::view::columnAlpha(coherence, columnForBin, 3);
    REQUIRE(alpha.size() == 3u);
    CHECK(alpha[0] == Catch::Approx(rta::view::kUntrustedAlphaFloor));
    CHECK(alpha[2] == Catch::Approx(rta::view::kUntrustedAlphaFloor));
}

// CATCHES: the exact barcode record §5a describes -- an unbridged
// implementation would paint column 1 at the floor (0.25) instead of the
// midpoint between its two measured neighbours, because that column has no
// bin of its own.
TEST_CASE("a column with no bins BETWEEN two measured columns bridges their trust",
          "[coherence-alpha]") {
    // Column 0: gamma^2 = 1.0 -> alpha 1.0. Column 2: gamma^2 = 0.0 -> alpha
    // at the floor, 0.25. Column 1 has no bin at all. bridgeGaps' own
    // single-gap formula (TraceDecimator.h) puts a one-column gap exactly
    // halfway between its neighbours.
    const std::vector<float> coherence{ 1.0f, 0.0f };
    const std::vector<int> columnForBin{ 0, 2 };
    const auto alpha = rta::view::columnAlpha(coherence, columnForBin, 3);
    REQUIRE(alpha.size() == 3u);
    CHECK(alpha[0] == Catch::Approx(1.0f));
    CHECK(alpha[2] == Catch::Approx(rta::view::kUntrustedAlphaFloor));
    const float expectedMidpoint = 0.5f * (1.0f + rta::view::kUntrustedAlphaFloor);
    CHECK(alpha[1] == Catch::Approx(expectedMidpoint));
    // And it is genuinely bridged, not just "not the floor": the midpoint
    // must sit strictly between the two neighbours, not merely differ from
    // the floor by accident.
    CHECK(alpha[1] > rta::view::kUntrustedAlphaFloor);
    CHECK(alpha[1] < 1.0f);
}

TEST_CASE("no coherence at all means full confidence is never assumed",
          "[coherence-alpha]") {
    // A trace that carries no coherence field is not a trace measured at
    // gamma^2 = 1. Callers pass an empty span and get an empty result, and
    // task 7's stroking treats an EMPTY alpha vector as "this quantity has no
    // trust information" -- which is a different thing from "trust is zero"
    // and is drawn opaque, because dimming a single-channel RTA capture would
    // be asserting a measurement that was never taken.
    CHECK(rta::view::columnAlpha({}, {}, 4).empty());
}
