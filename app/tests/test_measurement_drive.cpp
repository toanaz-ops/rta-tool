// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H7 task T13 (docs/plans/2026-10-21-h7-generator-output-plan.md, owner
// decision D1): the ONE function that says what level and outputs a
// measurement plays. Expectations are closed forms: a measurement source is
// built the way `makeSource` builds the generator's -- noise by its RMS, a
// sweep by its peak, amplitude 10^(dB/20) -- never a value the code printed.

#include "measure/MeasurementDrive.h"

#include "rta/gen/Noise.h"
#include "rta/gen/Prng.h"
#include "rta/gen/Sweep.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace rta::measure;

namespace {

double amp(double db) { return std::pow(10.0, db / 20.0); }

}  // namespace

TEST_CASE("measurement drive: FOLLOW GEN takes the generator's level and outputs") {
    GeneratorSettings gen;
    gen.levelDb = -30.0;
    gen.outputs = {1, 0};
    const MeasurementDrive drive = measurementDrive(MeasurementLevelMode::FollowGen, gen, 2);
    CHECK(drive.levelDb == -30.0);
    CHECK(drive.outputs == std::vector<int>{0, 1});  // sorted, as the generator routes them
}

TEST_CASE("measurement drive: FOLLOW GEN is bounded by the same -12 ceiling and -60 floor") {
    GeneratorSettings gen;
    gen.levelDb = 0.0;  // a request above the ceiling
    CHECK(measurementDrive(MeasurementLevelMode::FollowGen, gen, 2).levelDb == kGeneratorCeilingDb);
    gen.levelDb = -200.0;
    CHECK(measurementDrive(MeasurementLevelMode::FollowGen, gen, 2).levelDb == kGeneratorFloorDb);
    gen.levelDb = std::numeric_limits<double>::quiet_NaN();
    CHECK(measurementDrive(MeasurementLevelMode::FollowGen, gen, 2).levelDb == kGeneratorFloorDb);
}

TEST_CASE("measurement drive: outputs the device does not have are dropped; none left reads as out 1") {
    GeneratorSettings gen;
    gen.outputs = {0, 5};
    CHECK(measurementDrive(MeasurementLevelMode::FollowGen, gen, 2).outputs == std::vector<int>{0});
    gen.outputs = {7};
    CHECK(measurementDrive(MeasurementLevelMode::FollowGen, gen, 2).outputs == std::vector<int>{0});
}

TEST_CASE("measurement drive: OWN is -12 dBFS and no outputs, whatever the generator says") {
    GeneratorSettings gen;
    gen.levelDb = -45.0;
    gen.outputs = {1};
    const MeasurementDrive drive = measurementDrive(MeasurementLevelMode::Own, gen, 2);
    CHECK(drive.levelDb == kOwnMeasurementLevelDb);
    CHECK(kOwnMeasurementLevelDb == -12.0);
    CHECK(drive.outputs.empty());  // empty = the measurement keeps its own output
}

TEST_CASE("measurement drive: a measurement is never louder than the pre-H7 -12 dBFS") {
    // FOLLOW GEN's ceiling and OWN's level are the same number, so switching
    // modes can only lower a measurement, never raise it past what it was.
    CHECK(kGeneratorCeilingDb == kOwnMeasurementLevelDb);
}

TEST_CASE("measurement drive: the level text names its convention, one decimal") {
    CHECK(measurementLevelText(-30.0, LevelConvention::Peak) == "-30.0 dBFS peak");
    CHECK(measurementLevelText(-12.0, LevelConvention::Rms) == "-12.0 dBFS RMS");
    CHECK(measurementDriveText(-30.0, LevelConvention::Peak, {0}) == "-30.0 dBFS peak, out 1");
    CHECK(measurementDriveText(-30.0, LevelConvention::Rms, {0, 1}) == "-30.0 dBFS RMS, out 1+2");
    CHECK(measurementDriveText(-30.0, LevelConvention::Rms, {}) == "-30.0 dBFS RMS");
}

TEST_CASE("measurement drive: noise built at the drive's level is that RMS, a sweep that peak") {
    GeneratorSettings gen;
    gen.levelDb = -30.0;
    const MeasurementDrive drive = measurementDrive(MeasurementLevelMode::FollowGen, gen, 2);

    // Pink, the way LOCATE / VERIFY / ALIGN build it: RMS over 2^18 samples.
    constexpr std::size_t kN = 1u << 18;
    rta::gen::PinkNoise pink(rta::gen::Pcg32(7, 1), drive.levelDb);
    std::vector<float> noise(kN, 0.0f);
    pink.process(noise);
    double acc = 0.0;
    for (const float v : noise) acc += static_cast<double>(v) * v;
    CHECK(std::abs(10.0 * std::log10(acc / static_cast<double>(kN)) - (-30.0)) <= 0.2);

    // The sweep, the way SWEEP builds it: peak amplitude 10^(dB/20). A short
    // sweep keeps the case fast; the peak is a property of the amplitude.
    rta::gen::Sweep::Config config;
    config.durationSec = 1.0;
    config.levelDbFsPeak = drive.levelDb;
    rta::gen::Sweep sweep(config);
    std::vector<float> samples(static_cast<std::size_t>(config.sampleRate * sweep.durationSec()), 0.0f);
    sweep.process(samples);
    float peak = 0.0f;
    for (const float v : samples) peak = std::max(peak, std::abs(v));
    CHECK(std::abs(20.0 * std::log10(static_cast<double>(peak)) - (-30.0)) <= 0.1);
    CHECK(peak <= static_cast<float>(amp(-30.0)) * 1.0001f);  // and never above it
}
