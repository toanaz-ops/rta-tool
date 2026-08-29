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

TEST_CASE("a column with no bins reports no trust", "[coherence-alpha]") {
    // The extent's own hasData decides whether the column draws at all; this
    // only has to avoid handing back a confident alpha for a column nothing
    // measured.
    const std::vector<float> coherence{ 0.9f, 0.9f };
    const std::vector<int> columnForBin{ 0, 2 };
    const auto alpha = rta::view::columnAlpha(coherence, columnForBin, 3);
    REQUIRE(alpha.size() == 3u);
    CHECK(alpha[1] == Catch::Approx(rta::view::kUntrustedAlphaFloor));
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
