// SPDX-License-Identifier: AGPL-3.0-or-later
#include "trace/Trace.h"

#include <catch2/catch_test_macros.hpp>

using rta::trace::CaptureMeta;
using rta::trace::Field;
using rta::trace::LevelUnit;
using rta::trace::Trace;

namespace {
CaptureMeta meta(int fftSize = 8, double sampleRate = 48000.0) {
    CaptureMeta m;
    m.id = "trace-0001";
    m.fftSize = fftSize;
    m.sampleRate = sampleRate;
    return m;
}
}  // namespace

TEST_CASE("pointCountFor is the real-FFT bin count", "[trace]") {
    CHECK(rta::trace::pointCountFor(8) == 5u);
    CHECK(rta::trace::pointCountFor(32768) == 16385u);
    CHECK(rta::trace::pointCountFor(0) == 0u);
}

TEST_CASE("a magnitude of the wrong length is refused outright", "[trace]") {
    CHECK_FALSE(Trace::make(meta(), std::vector<float>(4, 0.0f)).has_value());
    CHECK(Trace::make(meta(), std::vector<float>(5, 0.0f)).has_value());
}

TEST_CASE("absent fields report absence, not zeros", "[trace]") {
    auto t = Trace::make(meta(), std::vector<float>(5, -30.0f));
    REQUIRE(t.has_value());
    CHECK(t->has(Field::Magnitude));
    CHECK_FALSE(t->has(Field::Phase));
    CHECK_FALSE(t->has(Field::Coherence));
    // The whole point of the presence model: a consumer asking for a field a
    // single-channel capture never had gets nothing, not a plausible zero.
    CHECK(t->field(Field::Phase).empty());
    CHECK(t->field(Field::Coherence).empty());
}

TEST_CASE("a second field must match the magnitude's length", "[trace]") {
    auto t = Trace::make(meta(), std::vector<float>(5, -30.0f));
    REQUIRE(t.has_value());
    CHECK_FALSE(t->setPhase(std::vector<float>(4, 0.0f)));
    CHECK_FALSE(t->has(Field::Phase));
    CHECK(t->setPhase(std::vector<float>(5, 1.5f)));
    CHECK(t->has(Field::Phase));
    CHECK(t->field(Field::Phase).size() == 5u);
}

TEST_CASE("binHz comes from the stored rate and size, not a stored axis", "[trace]") {
    auto t = Trace::make(meta(8, 48000.0), std::vector<float>(5, 0.0f));
    REQUIRE(t.has_value());
    CHECK(t->binHz() == 6000.0);
}

TEST_CASE("the calibration unit travels with the offset", "[trace]") {
    CaptureMeta m = meta();
    m.calibrationOffsetDb = 94.0f;
    m.calibrationUnit = LevelUnit::DbSpl;
    auto t = Trace::make(m, std::vector<float>(5, 0.0f));
    REQUIRE(t.has_value());
    CHECK(t->meta().calibrationUnit == LevelUnit::DbSpl);
}
