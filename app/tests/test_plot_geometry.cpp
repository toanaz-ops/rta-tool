// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Closed-form geometry: a logarithmic frequency axis is defined by "a
// decade's geometric mean sits at its midpoint" -- that is the one property
// a linear axis would fail while passing every pointwise sample check, so it
// gets its own test rather than folding into the round-trip check.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "view/PlotGeometry.h"

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using namespace rta::view;

namespace {

PlotGeometry testGeometry() {
    PlotGeometry g;
    g.left = 40.0f;
    g.right = 1040.0f;
    g.top = 20.0f;
    g.bottom = 620.0f;
    g.fLowHz = 20.0;
    g.fHighHz = 20000.0;
    g.dbTop = 0.0;
    g.dbBottom = -90.0;
    return g;
}

}  // namespace

TEST_CASE("The axis ends land exactly on the plot edges", "[plot-geometry]") {
    const auto g = testGeometry();
    CHECK_THAT((double) g.xForHz(g.fLowHz), WithinAbs((double) g.left, 1e-4));
    CHECK_THAT((double) g.xForHz(g.fHighHz), WithinAbs((double) g.right, 1e-4));
}

TEST_CASE("A decade's geometric mean sits at its midpoint", "[plot-geometry]") {
    const auto g = testGeometry();
    const double mid = ((double) g.xForHz(100.0) + (double) g.xForHz(1000.0)) / 2.0;
    CHECK_THAT((double) g.xForHz(std::sqrt(100.0 * 1000.0)), WithinAbs(mid, 1e-4));
}

TEST_CASE("Hz and x round-trip", "[plot-geometry]") {
    const auto g = testGeometry();
    for (int i = 0; i < 40; ++i) {
        const double t = (double) i / 39.0;
        const double hz = g.fLowHz * std::pow(g.fHighHz / g.fLowHz, t);
        const double roundTripped = g.hzForX(g.xForHz(hz));
        CAPTURE(hz);
        CHECK_THAT(roundTripped, WithinRel(hz, 1e-6));
    }
}

TEST_CASE("dB maps top-down and clamps", "[plot-geometry]") {
    const auto g = testGeometry();
    CHECK_THAT((double) g.yForDb(g.dbTop), WithinAbs((double) g.top, 1e-4));
    CHECK_THAT((double) g.yForDb(g.dbBottom), WithinAbs((double) g.bottom, 1e-4));
    // 40 dB past the floor must clamp, not draw off the bottom of the plot.
    CHECK_THAT((double) g.yForDb(g.dbBottom - 40.0), WithinAbs((double) g.bottom, 1e-4));
}

TEST_CASE("decadeTicks yields the 1-2-5 sequence in range", "[plot-geometry]") {
    const auto ticks = decadeTicks(20.0, 20000.0);
    const std::vector<double> expected = {
        20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000
    };
    REQUIRE(ticks.size() == expected.size());
    for (std::size_t i = 0; i < ticks.size(); ++i) {
        CAPTURE(i);
        CHECK(ticks[i] == expected[i]);
    }
}
