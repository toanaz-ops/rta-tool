// SPDX-License-Identifier: AGPL-3.0-or-later
#include "view/TraceDecimator.h"

#include <catch2/catch_test_macros.hpp>

using rta::view::bridgeGaps;
using rta::view::ColumnExtent;
using rta::view::decimateToColumns;

namespace {
ColumnExtent filled(float v) { return ColumnExtent{ v, v, true }; }
ColumnExtent empty() { return ColumnExtent{}; }
}  // namespace

TEST_CASE("each column brackets the true min and max of its bins", "[decimator]") {
    // Four bins into two columns: {3, -1} and {7, 2}.
    const std::vector<float> values{3.0f, -1.0f, 7.0f, 2.0f};
    const std::vector<int> columnForBin{0, 0, 1, 1};

    const auto out = decimateToColumns(values, columnForBin, 2);
    REQUIRE(out.size() == 2u);

    CHECK(out[0].hasData);
    CHECK(out[0].minValue <= -1.0f);
    CHECK(out[0].maxValue >= 3.0f);
    CHECK(out[1].minValue <= 2.0f);
    CHECK(out[1].maxValue >= 7.0f);
}

TEST_CASE("a column with no bins reports no data rather than zero", "[decimator]") {
    const std::vector<float> values{5.0f};
    const std::vector<int> columnForBin{2};

    const auto out = decimateToColumns(values, columnForBin, 3);
    REQUIRE(out.size() == 3u);
    CHECK_FALSE(out[0].hasData);
    CHECK_FALSE(out[1].hasData);
    CHECK(out[2].hasData);
    // A gap must not draw as a line to 0 dB -- that is a null the engineer
    // would read as real.
    CHECK(out[2].minValue == 5.0f);
}

TEST_CASE("a narrow null survives decimation", "[decimator]") {
    // The reason this is min/max and not one sample per column: the engineer
    // is looking for exactly this bin.
    std::vector<float> values(100, 0.0f);
    values[57] = -40.0f;
    std::vector<int> columnForBin(100, 0);
    for (int i = 0; i < 100; ++i) columnForBin[static_cast<std::size_t>(i)] = i / 10;

    const auto out = decimateToColumns(values, columnForBin, 10);
    REQUIRE(out.size() == 10u);
    CHECK(out[5].minValue == -40.0f);
}

TEST_CASE("mismatched or empty inputs yield nothing, not garbage", "[decimator]") {
    const std::vector<float> values{1.0f, 2.0f};
    const std::vector<int> shortMap{0};
    CHECK(decimateToColumns(values, shortMap, 2).empty());
    CHECK(decimateToColumns(values, std::vector<int>{0, 0}, 0).empty());
}

TEST_CASE("an out-of-range column index is skipped, not written past", "[decimator]") {
    const std::vector<float> values{1.0f, 2.0f};
    const std::vector<int> columnForBin{0, 9};
    const auto out = decimateToColumns(values, columnForBin, 1);
    REQUIRE(out.size() == 1u);
    CHECK(out[0].hasData);
    CHECK(out[0].maxValue == 1.0f);
}

// CATCHES: an implementation that copies a neighbour verbatim instead of
// interpolating -- e.g. fill-forward, which would leave this column at 2.0
// (the left neighbour) rather than the midpoint.
TEST_CASE("a single empty column between two filled ones gets the midpoint",
          "[decimator][bridge]") {
    std::vector<ColumnExtent> columns{ filled(2.0f), empty(), filled(6.0f) };
    const auto out = bridgeGaps(std::move(columns));

    REQUIRE(out.size() == 3u);
    CHECK(out[1].hasData);
    CHECK(out[1].minValue == 4.0f);
    CHECK(out[1].maxValue == 4.0f);
}

// CATCHES: an implementation that only handles a one-column gap correctly
// (e.g. always averaging the immediate left/right neighbours regardless of
// gap width) and produces a step rather than a ramp across a wider run.
TEST_CASE("a run of three empty columns interpolates linearly across the run",
          "[decimator][bridge]") {
    std::vector<ColumnExtent> columns{ filled(0.0f), empty(), empty(), empty(), filled(8.0f) };
    const auto out = bridgeGaps(std::move(columns));

    REQUIRE(out.size() == 5u);
    CHECK(out[1].hasData);
    CHECK(out[2].hasData);
    CHECK(out[3].hasData);
    CHECK(out[1].minValue == 2.0f);
    CHECK(out[2].minValue == 4.0f);
    CHECK(out[3].minValue == 6.0f);
}

// CATCHES: fill-forward, the "convenient" wrong implementation named in the
// brief. Fill-forward would leave the leading run empty by accident (there is
// nothing yet to forward from) but WOULD fill the trailing run by carrying the
// last real value past the end of the measured range -- which is exactly the
// invented data this function exists to refuse. Both ends are asserted so a
// fix that only gets leading right cannot pass by accident.
TEST_CASE("leading and trailing empty runs stay empty", "[decimator][bridge]") {
    std::vector<ColumnExtent> columns{ empty(), filled(3.0f), empty(), filled(9.0f), empty() };
    const auto out = bridgeGaps(std::move(columns));

    REQUIRE(out.size() == 5u);
    CHECK_FALSE(out[0].hasData);   // leading: nothing before it to bridge from
    CHECK(out[2].hasData);         // middle: has real neighbours on both sides
    CHECK_FALSE(out[4].hasData);   // trailing: nothing after it to bridge to
}

// CATCHES: an implementation that unconditionally rewrites every column (e.g.
// re-averaging a filled column with itself due to an off-by-one in the run
// bounds), which would leave gap-free input looking unchanged on casual
// inspection but subtly altered under closer comparison.
TEST_CASE("input with no gaps comes back unchanged", "[decimator][bridge]") {
    const std::vector<ColumnExtent> columns{ filled(1.0f), filled(2.0f), filled(3.0f) };
    const auto out = bridgeGaps(columns);

    REQUIRE(out.size() == columns.size());
    for (std::size_t i = 0; i < columns.size(); ++i) {
        CHECK(out[i].hasData == columns[i].hasData);
        CHECK(out[i].minValue == columns[i].minValue);
        CHECK(out[i].maxValue == columns[i].maxValue);
    }
}

// CATCHES: a loop that treats the whole input as one giant "gap" bounded by
// nothing and crashes or fabricates data when there is no filled column
// anywhere to anchor an interpolation.
TEST_CASE("entirely empty input comes back entirely empty", "[decimator][bridge]") {
    std::vector<ColumnExtent> columns(6, empty());
    const auto out = bridgeGaps(std::move(columns));

    REQUIRE(out.size() == 6u);
    for (const auto& c : out) CHECK_FALSE(c.hasData);
}
