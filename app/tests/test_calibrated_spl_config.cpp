// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Task W2-E2b fix round (verifier MEDIUM finding, mutant M2): the
// composition-root step "calibration START completed -> the SplConfig a
// fresh log should carry" is lifted into `calibratedSplLogConfig`
// (CalibratedSplConfig.h) so a dropped offset/calibrated/calibratorLevelDb
// assignment is caught here rather than silently shipping an uncalibrated
// log forever. JUCE-free.
#include "measure/CalibratedSplConfig.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/Weighting.h"
#include "rta/meter/Block.h"

#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::measure::calibratedSplLogConfig;
using rta::measure::CalibrationSession;
using rta::measure::calibrationLevel;
using rta::measure::kIec60942Level94Db;

namespace {

std::vector<float> sineAt1kHz(double amplitude, double sampleRate, std::size_t numSamples) {
    std::vector<float> x(numSamples);
    for (std::size_t n = 0; n < numSamples; ++n) {
        const double t = static_cast<double>(n) / sampleRate;
        x[n] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * 1000.0 * t));
    }
    return x;
}

}  // namespace

TEST_CASE("calibratedSplLogConfig carries the session's own offset and calibrated flag, "
         "never SplConfig{}'s uncalibrated defaults",
         "[calibrated_spl_config]") {
    // Mutant M2's own shape: a dropped `config.referenceOffsetDb = ...` line
    // compiles clean and leaves `config` at SplConfig{}'s default (0.0,
    // uncalibrated) -- this fixture's offset is non-zero and distinguishable
    // from that default so the mutation is actually visible.
    constexpr double kFs = 48000.0;
    CalibrationSession session;
    session.recordStartCheck(calibrationLevel(kIec60942Level94Db), sineAt1kHz(0.5, kFs, 48000), kFs,
                             1'700'000'000'000ull);
    REQUIRE(session.hasStartCheck());
    REQUIRE(session.referenceOffsetDb() != 0.0);  // the fixture's own precondition

    const auto result = calibratedSplLogConfig(session);
    CHECK_THAT(result.config.referenceOffsetDb, WithinAbs(session.referenceOffsetDb(), 1e-12));
    CHECK(result.config.calibrated);
    CHECK_THAT(result.calibratorLevelDb, WithinAbs(kIec60942Level94Db, 1e-12));
}

TEST_CASE("calibratedSplLogConfig's config is otherwise SplConfig{} defaults, SPL-R11",
         "[calibrated_spl_config]") {
    constexpr double kFs = 48000.0;
    CalibrationSession session;
    session.recordStartCheck(calibrationLevel(kIec60942Level94Db), sineAt1kHz(0.5, kFs, 48000), kFs, 0);

    const auto result = calibratedSplLogConfig(session);
    const rta::measure::SplConfig defaults;
    CHECK_THAT(result.config.blockSeconds, WithinAbs(defaults.blockSeconds, 1e-12));
    CHECK(result.config.metrics.empty());
    CHECK(result.config.alarms.empty());
}
