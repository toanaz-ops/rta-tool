// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Smaart LE v9.1 p.78's own criterion (record §8.1): three or more
// consecutive samples at or above the coarsest integer full scale a
// converter can deliver. Every threshold here is a closed-form power of two,
// not "whatever the code printed" -- see OverloadDetector.h for the exact
// representability argument.

#include "rta/dsp/OverloadDetector.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

using namespace rta::dsp;

TEST_CASE("a full-scale run of three or more flags; shorter or below threshold does not",
          "[overload][threshold]") {
    constexpr float t = kFullScaleThreshold;
    REQUIRE(hasOverload(std::vector<float>{t, t, t}));
    REQUIRE_FALSE(hasOverload(std::vector<float>{t, t}));

    // 1 - 2^-14: a coarser (larger) gap from full scale than the threshold's
    // own 2^-15, so this sits strictly BELOW kFullScaleThreshold.
    constexpr float belowThreshold = 1.0f - 0x1p-14f;
    REQUIRE(belowThreshold < t);
    REQUIRE_FALSE(hasOverload(std::vector<float>{belowThreshold, belowThreshold, belowThreshold}));

    REQUIRE(hasOverload(std::vector<float>{-t, -t, -t}));  // |x| >= threshold, not x >= threshold

    // No state between calls: a run split across the boundary of two
    // separate hops does not carry over (record §8's capture-window latch is
    // what stitches runs across hops, one layer up in app/).
    REQUIRE_FALSE(hasOverload(std::vector<float>{0.0f, 0.0f, t, t}));
    REQUIRE_FALSE(hasOverload(std::vector<float>{t, 0.0f, 0.0f, 0.0f}));
}

TEST_CASE("every integer full scale flags; a sub-threshold sine never does",
          "[overload][fullscale]") {
    constexpr float int16FullScale = 1.0f - 0x1p-15f;
    constexpr float int24FullScale = 1.0f - 0x1p-23f;
    constexpr float int32FullScale = 1.0f - 0x1p-31f;  // rounds to 1.0f in float32
    constexpr float floatFullScale = 1.0f;
    REQUIRE(int32FullScale == 1.0f);  // the rounding OverloadDetector.h's comment claims

    for (const float level : {int16FullScale, int24FullScale, int32FullScale, floatFullScale}) {
        CAPTURE(level);
        REQUIRE(hasOverload(std::vector<float>{level, level, level}));
    }

    // A sine peaking strictly below kFullScaleThreshold must never flag, at
    // any phase -- its true peak never crosses the line, however densely
    // sampled the run around that peak is.
    constexpr float peak = 1.0f - 0x1p-14f;
    for (const double phaseOffset : {0.0, 0.37, 1.1, 2.9}) {
        CAPTURE(phaseOffset);
        std::vector<float> sine(64);
        for (std::size_t i = 0; i < sine.size(); ++i) {
            const double angle =
                2.0 * std::numbers::pi * static_cast<double>(i) / 32.0 + phaseOffset;
            sine[i] = peak * static_cast<float>(std::sin(angle));
        }
        REQUIRE_FALSE(hasOverload(sine));
    }

    REQUIRE_FALSE(hasOverload(std::span<const float>{}));
    REQUIRE(hasOverload(std::vector<float>{kFullScaleThreshold}, 1));
}
