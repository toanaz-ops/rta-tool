// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests. Task 7 of
// docs/plans/2026-09-05-L3-mtw-impl-plan.md: the shared bin/frequency ->
// column mapping, JUCE-free so it runs under RTA_BUILD_APP=OFF.
#include "view/ColumnMap.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using rta::view::absoluteColumnsForBins;
using rta::view::absoluteColumnsForFrequencies;
using rta::view::PlotGeometry;

namespace {

PlotGeometry testGeometry() {
    PlotGeometry g;
    g.left = 40.0f;
    g.right = 1060.0f;
    g.top = 0.0f;
    g.bottom = 400.0f;
    g.fLowHz = 20.0;
    g.fHighHz = 20000.0;
    return g;
}

}  // namespace

// CATCHES: the refactor changing the fixed path's own behaviour -- a
// hand-copied second loop that rounds one step differently would still look
// plausible while silently moving every existing live-trace pixel.
TEST_CASE("an explicit uniform frequency vector maps to the same columns as a bin width",
          "[columnmap]") {
    const PlotGeometry g = testGeometry();
    constexpr double binHz = 46.875;
    constexpr std::size_t bins = 513;
    constexpr std::size_t columnCount = 1100;

    std::vector<double> hz(bins);
    for (std::size_t i = 0; i < bins; ++i) hz[i] = static_cast<double>(i) * binHz;

    const auto viaFrequencies = absoluteColumnsForFrequencies(g, hz, columnCount);
    const auto viaBins = absoluteColumnsForBins(g, binHz, bins, columnCount);

    REQUIRE(viaFrequencies.size() == viaBins.size());
    for (std::size_t i = 0; i < bins; ++i) {
        CAPTURE(i);
        CHECK(viaFrequencies[i] == viaBins[i]);
    }
}

// CATCHES: a caller reaching PlotGeometry::xForHz(0.0) directly -- log(0) is
// non-finite, and letting that reach a juce::Path is the exact defect the
// MTW block's own hz[0] == 0 (DC) would trigger.
TEST_CASE("a zero-hertz point produces no column", "[columnmap]") {
    const PlotGeometry g = testGeometry();
    const std::vector<double> hz{ 0.0, 187.5, 375.0, 6000.0 };
    const auto columns = absoluteColumnsForFrequencies(g, hz, 1100);

    REQUIRE(columns.size() == hz.size());
    CHECK(columns[0] == -1);
    for (std::size_t i = 1; i < hz.size(); ++i) {
        CAPTURE(i);
        CHECK(columns[i] != -1);
        CHECK(std::isfinite(static_cast<double>(columns[i])));
    }
}
