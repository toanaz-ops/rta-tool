// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Closed-form: a full-scale sine has mean square 0.5, so `levelDbFs(0.5)`
// must read 0.0 exactly, by the dBFS convention this file pins down (plan
// §1.4: 10*log10(power) + 3.0103).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "measure/Levels.h"

#include <cmath>
#include <limits>

using Catch::Matchers::WithinAbs;
using namespace rta::measure;

TEST_CASE("A full-scale sine reads 0.0 dBFS", "[levels]") {
    CHECK_THAT(levelDbFs(0.5), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Silence reads the floor, not negative infinity", "[levels]") {
    const double db = levelDbFs(0.0);
    CHECK(db == kLevelFloorDb);
    CHECK(std::isfinite(db));
}

TEST_CASE("Ten times the power is ten dB", "[levels]") {
    CHECK_THAT(levelDbFs(0.05) - levelDbFs(0.5), WithinAbs(-10.0, 1e-9));
}

TEST_CASE("Negative or NaN power clamps to the floor, never NaN or -inf", "[levels]") {
    CHECK(levelDbFs(-1.0) == kLevelFloorDb);
    const double fromNan = levelDbFs(std::numeric_limits<double>::quiet_NaN());
    CHECK(fromNan == kLevelFloorDb);
    CHECK(std::isfinite(fromNan));
}

TEST_CASE("clampLevelDb never returns below the floor or a non-finite value", "[levels]") {
    CHECK(clampLevelDb(-500.0) == kLevelFloorDb);
    CHECK(clampLevelDb(0.0) == 0.0);
    CHECK(clampLevelDb(std::numeric_limits<double>::infinity()) == kLevelFloorDb);
}
