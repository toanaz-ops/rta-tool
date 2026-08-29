// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests. Decision 5 of
// docs/dsp/2026-08-29-display-layer-l5c.md.
#include "view/PhaseDecimator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using rta::view::DrawnPhaseColumn;
using rta::view::PhaseColumn;

TEST_CASE("wrapTo180 uses core's own half-open convention", "[phase-decimator]") {
    // rta::dsp::TransferSnapshot::phaseRadians is documented as wrapped to
    // (-pi, pi]. Matching that here -- rather than [-180, 180) -- means a
    // value that survived the engine's wrap is a fixed point of this one, so a
    // trace can be wrapped twice with no drift at the boundary.
    CHECK(rta::view::wrapTo180(0.0f) == Catch::Approx(0.0f));
    CHECK(rta::view::wrapTo180(180.0f) == Catch::Approx(180.0f));
    CHECK(rta::view::wrapTo180(-180.0f) == Catch::Approx(180.0f));
    CHECK(rta::view::wrapTo180(190.0f) == Catch::Approx(-170.0f));
    CHECK(rta::view::wrapTo180(-190.0f) == Catch::Approx(170.0f));
    CHECK(rta::view::wrapTo180(725.0f) == Catch::Approx(5.0f));
}

TEST_CASE("unwrapAlongBins recovers a pure delay's straight line",
          "[phase-decimator]") {
    // Closed form, exact by construction. A pure delay of D samples has
    // phi(f) = -2*pi*f*D/fs; at bin k of an N-point transform, f = k*fs/N, so
    //     phi(k) = -360 * k * D / N  degrees,
    // with no reference to fs at all. Choosing D/N = 64/1024 = 1/16 makes that
    // -22.5 degrees per bin EXACTLY -- an exact float, so the assertion needs
    // no tolerance to hide a rounding bug behind, and every step is 22.5 deg,
    // comfortably under the 180 deg the unwrap needs to stay unambiguous.
    constexpr int kBins = 65;
    constexpr float kDegPerBin = -22.5f;

    std::vector<float> wrapped(kBins);
    for (int k = 0; k < kBins; ++k) {
        wrapped[static_cast<std::size_t>(k)] =
            rta::view::wrapTo180(kDegPerBin * static_cast<float>(k));
    }

    const auto unwrapped = rta::view::unwrapAlongBins(wrapped);
    REQUIRE(unwrapped.size() == wrapped.size());
    for (int k = 0; k < kBins; ++k) {
        CHECK(unwrapped[static_cast<std::size_t>(k)]
              == Catch::Approx(kDegPerBin * static_cast<float>(k)).margin(1e-3));
    }
}

TEST_CASE("a column of alternating +179/-179 spans two degrees, not 358",
          "[phase-decimator]") {
    // THE test this file exists for. Min/max taken on WRAPPED values reports
    // an extent of ~358 degrees -- a near-full-height band drawn across the
    // pane -- for data that never moved more than two degrees. It is listed
    // explicitly, like the dual-FFT record's identically-1.0 coherence test,
    // because it is the failure a plausible implementation actually makes.
    const std::vector<float> wrapped{ 179.0f, -179.0f, 179.0f, -179.0f, 179.0f };
    const std::vector<int> columnForBin{ 0, 0, 0, 0, 0 };

    const auto columns = rta::view::decimatePhaseToColumns(wrapped, columnForBin, 1);
    REQUIRE(columns.size() == 1u);
    REQUIRE(columns[0].hasData);
    CHECK((columns[0].maxDeg - columns[0].minDeg) == Catch::Approx(2.0f).margin(1e-3));

    // And at draw time it is a wrap straddle -- two short pieces at the top and
    // bottom edges of the pane -- never a full-height band.
    const auto drawn = rta::view::wrapForDrawing(columns);
    REQUIRE(drawn.size() == 1u);
    CHECK(drawn[0].straddlesWrap);
    CHECK_FALSE(drawn[0].fullBand);
}

TEST_CASE("a column rotating more than a full turn draws as a band",
          "[phase-decimator]") {
    // Decision 5: phase rotating faster than one pixel column can resolve is
    // honestly rendered as a full-height band. Built from -100 deg steps so
    // every individual step stays unambiguous to the unwrap while the column's
    // total extent reaches 400 deg.
    std::vector<float> wrapped;
    for (int i = 0; i < 5; ++i) {
        wrapped.push_back(rta::view::wrapTo180(static_cast<float>(-100 * i)));
    }
    const std::vector<int> columnForBin(wrapped.size(), 0);

    const auto columns = rta::view::decimatePhaseToColumns(wrapped, columnForBin, 1);
    REQUIRE(columns.size() == 1u);
    CHECK((columns[0].maxDeg - columns[0].minDeg) == Catch::Approx(400.0f).margin(1e-3));

    const auto drawn = rta::view::wrapForDrawing(columns);
    REQUIRE(drawn[0].fullBand);
    CHECK(drawn[0].minDeg == Catch::Approx(-180.0f));
    CHECK(drawn[0].maxDeg == Catch::Approx(180.0f));
}

TEST_CASE("the pen lifts exactly where the drawn trace wraps", "[phase-decimator]") {
    // A -50 deg/bin ramp, two bins per column, four columns. Unwrapped column
    // midpoints are -25, -125, -225, -325; wrapped they are -25, -125, +135,
    // +35. Only the -125 -> +135 step exceeds 180 degrees, so exactly one
    // interior pen lift is correct. Every number here is exact.
    std::vector<float> wrapped;
    for (int k = 0; k < 8; ++k) {
        wrapped.push_back(rta::view::wrapTo180(static_cast<float>(-50 * k)));
    }
    const std::vector<int> columnForBin{ 0, 0, 1, 1, 2, 2, 3, 3 };

    const auto drawn =
        rta::view::wrapForDrawing(rta::view::decimatePhaseToColumns(wrapped, columnForBin, 4));
    REQUIRE(drawn.size() == 4u);

    CHECK(drawn[0].penLift);        // nothing to the left to connect to
    CHECK_FALSE(drawn[1].penLift);
    CHECK(drawn[2].penLift);        // -125 -> +135 is the wrap
    CHECK_FALSE(drawn[3].penLift);
}

TEST_CASE("a column no bin lands in draws nothing", "[phase-decimator]") {
    // Same rule TraceDecimator states for magnitude: absence is not zero. A
    // phase of 0 degrees drawn where nothing was measured is a straight line
    // through the middle of the pane, which reads as a perfectly aligned
    // system.
    const std::vector<float> wrapped{ 10.0f, 20.0f };
    const std::vector<int> columnForBin{ 0, 2 };

    const auto columns = rta::view::decimatePhaseToColumns(wrapped, columnForBin, 3);
    REQUIRE(columns.size() == 3u);
    CHECK(columns[0].hasData);
    CHECK_FALSE(columns[1].hasData);
    CHECK(columns[2].hasData);

    const auto drawn = rta::view::wrapForDrawing(columns);
    CHECK_FALSE(drawn[1].hasData);
    // The column after a hole cannot connect across it either.
    CHECK(drawn[2].penLift);
}

TEST_CASE("out-of-range column indices are skipped, not crashed on",
          "[phase-decimator]") {
    // Same contract decimateToColumns documents: the mapping is the caller's,
    // and a bin outside the plotted range carries -1.
    const std::vector<float> wrapped{ 10.0f, 20.0f, 30.0f };
    const std::vector<int> columnForBin{ -1, 0, 7 };
    const auto columns = rta::view::decimatePhaseToColumns(wrapped, columnForBin, 2);
    REQUIRE(columns.size() == 2u);
    CHECK(columns[0].hasData);
    CHECK_FALSE(columns[1].hasData);
}

TEST_CASE("mismatched inputs produce nothing rather than a guess",
          "[phase-decimator]") {
    const std::vector<float> wrapped{ 10.0f, 20.0f };
    const std::vector<int> shortMapping{ 0 };
    CHECK(rta::view::decimatePhaseToColumns(wrapped, shortMapping, 2).empty());
    CHECK(rta::view::decimatePhaseToColumns(wrapped, std::vector<int>{ 0, 0 }, 0).empty());
}
