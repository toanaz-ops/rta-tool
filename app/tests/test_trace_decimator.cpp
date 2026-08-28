// SPDX-License-Identifier: AGPL-3.0-or-later
#include "view/TraceDecimator.h"

#include <catch2/catch_test_macros.hpp>

using rta::view::ColumnExtent;
using rta::view::decimateToColumns;

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
