// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L6a task W3-A (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md; record
// docs/dsp/2026-09-16-spl-pro-l6a.md §8, §13 Q2): calibration as a flow -- a
// start/end pair, a drift, and ISO 1996-2 cl. 5.2's 0.5 dB as the only
// published criterion.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "CodeLines.h"

#include "measure/CalibrationSession.h"
#include "measure/Levels.h"

#include "rta/meter/Block.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <numbers>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::measure::calibrationLevel;
using rta::measure::CalibrationSession;
using rta::measure::CalibrationVerdict;
using rta::measure::kFullScaleSineOffsetDb;
using rta::measure::kIec60942Level114Db;
using rta::measure::kIec60942Level94Db;

namespace {

constexpr double kFs = 48000.0;

/// A `amplitude`-scaled 1 kHz sine, one second, 48 000 samples. Reused
/// verbatim from test_spl_seam.cpp's own `unitSine()` shape: a whole number
/// of periods, so the mean square is exact UP TO the float32 quantisation
/// `SplMeter::push` imposes on every sample (record §8's identity is proven
/// against exactly this floor, not a tighter one -- see A1 below).
std::vector<float> sineAt1kHz(double amplitude, double sampleRate) {
    std::vector<float> x(static_cast<std::size_t>(sampleRate));
    for (std::size_t n = 0; n < x.size(); ++n) {
        const double t = static_cast<double>(n) / sampleRate;
        x[n] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * 1000.0 * t));
    }
    return x;
}

std::filesystem::path measureSrc() {
    return std::filesystem::path(RTA_REPO_ROOT) / "app" / "src" / "measure";
}

}  // namespace

// --- A1: the closed form, no microphone ----------------------------------

TEST_CASE("A1 the closed form, no microphone: a 1 kHz sine through the Z path", "[calibration]") {
    // Record §8: L_meas = 10log10(A^2/2) = 20log10(A) - kFullScaleSineOffsetDb.
    // An identity, not a golden vector.
    constexpr double amplitude = 0.5;
    const auto samples = sineAt1kHz(amplitude, kFs);
    const auto level = calibrationLevel(kIec60942Level94Db);

    CalibrationSession session;
    session.recordStartCheck(level, samples, kFs, 0);
    REQUIRE(session.hasStartCheck());

    const double expected = 20.0 * std::log10(amplitude) - kFullScaleSineOffsetDb;
    // TOLERANCE DEVIATION FROM THE PLAN'S 1e-12, MEASURED NOT ARGUED, same
    // shape as test_spl_seam.cpp's E2: CalibrationSession measures through
    // `SplMeter::push(std::span<const float>)`, so the sine is quantised to
    // float32 BEFORE the meter ever sees it -- up to half a float ULP of
    // relative amplitude error, ~2.6e-7 dB of mean-square error. 1e-12 is
    // unreachable through the real Z path for any signal that is not exactly
    // float32-representable, and this is the same measured bound E2 already
    // shipped rather than a new, weaker one invented for this task.
    INFO("residual = " << (session.startCheck().measuredLevelDb - expected));
    CHECK_THAT(session.startCheck().measuredLevelDb, WithinAbs(expected, 1e-6));

    // offset = L_cal - L_meas (record §8's one-line identity), and applying
    // it makes the same signal read L_cal -- the round trip test_block.cpp's
    // A7 already pins on calibrationOffsetDb itself, checked here through
    // the flow rather than the bare function.
    const double offset = session.referenceOffsetDb();
    CHECK(offset == level.nominalDb - session.startCheck().measuredLevelDb);
    CHECK_THAT(session.startCheck().measuredLevelDb + offset, WithinAbs(level.nominalDb, 1e-9));
}

// --- A2 / A3: the pair, the drift, and the verdict -----------------------

TEST_CASE("A2 the pair, and the clause it is compared against", "[calibration]") {
    // Same measured sample buffer at both ends, different NOMINAL levels --
    // the drift then comes ENTIRELY from the 0.3 dB the two nominals differ
    // by, with no sine/log10 measurement noise in the way (that noise is A1's
    // subject, not this one's).
    const auto samples = sineAt1kHz(0.5, kFs);
    CalibrationSession session;
    session.recordStartCheck(calibrationLevel(94.0), samples, kFs, 1000);
    session.recordEndCheck(calibrationLevel(94.3), samples, kFs, 2000);

    REQUIRE(session.hasStartCheck());
    REQUIRE(session.hasEndCheck());
    CHECK(session.startCheck().unixMs == 1000);
    CHECK(session.endCheck().unixMs == 2000);
    CHECK(session.startCheck().level.nominalDb == 94.0);
    CHECK(session.endCheck().level.nominalDb == 94.3);

    const auto drift = session.driftDb();
    REQUIRE(drift.has_value());
    CHECK_THAT(*drift, WithinAbs(0.3, 1e-9));

    REQUIRE(session.verdict().has_value());
    CHECK(*session.verdict() == CalibrationVerdict::Pass);  // 0.3 <= 0.5

    const auto fields = session.reportFields();
    CHECK(fields.performed);
    CHECK(fields.verdict == CalibrationVerdict::Pass);
    CHECK(fields.clause == CalibrationSession::kClause);
    // ISO 1996-2:2017 cl. 5.2, cited BY NUMBER (record §8): a class 1
    // IEC 60942 calibrator check at the start and end, <= 0.5 dB between two
    // consecutive checks with no adjustment between them.
    CHECK(fields.clause.find("5.2") != std::string_view::npos);
}

TEST_CASE("A3 drift does not silently invalidate the log", "[calibration]") {
    // Opposite-signed raw difference on purpose: the end nominal is LOWER
    // than the start's, so `offsetEnd - offsetStart` (no abs) would be
    // NEGATIVE and would read `<= 0.5` as Pass. This is exactly the fixture
    // that turns red if CalibrationSession::driftDb() drops its abs() --
    // the mutation this task recorded red, then green (PR description).
    const auto samples = sineAt1kHz(0.5, kFs);
    CalibrationSession session;
    session.recordStartCheck(calibrationLevel(94.0), samples, kFs, 0);
    session.recordEndCheck(calibrationLevel(90.0), samples, kFs, 3'600'000);

    const auto drift = session.driftDb();
    REQUIRE(drift.has_value());
    CHECK_THAT(*drift, WithinAbs(4.0, 1e-9));  // |90.0 - 94.0|, not -4.0

    REQUIRE(session.verdict().has_value());
    CHECK(*session.verdict() == CalibrationVerdict::Fail);

    // The instrument does not throw the evidence away on its own authority
    // (record §8): both checks are still readable after a FAILED verdict.
    CHECK(session.hasStartCheck());
    CHECK(session.hasEndCheck());
    const auto fields = session.reportFields();
    CHECK(fields.performed);
    CHECK(fields.verdict == CalibrationVerdict::Fail);
    CHECK_THAT(fields.driftDb, WithinAbs(4.0, 1e-9));
    CHECK(fields.start.level.nominalDb == 94.0);
    CHECK(fields.end.level.nominalDb == 90.0);
}

// --- A4: the nominal levels are IEC 60942's -------------------------------

TEST_CASE("A4 the nominal levels are IEC 60942's, and anything else is operator-supplied",
          "[calibration]") {
    CHECK_FALSE(calibrationLevel(kIec60942Level94Db).operatorSupplied);
    CHECK_FALSE(calibrationLevel(kIec60942Level114Db).operatorSupplied);

    // Any OTHER value is ACCEPTED -- never refused -- but recorded as
    // operator-supplied, and the number itself is never silently normalised
    // to one of the two (A4's own wording: "never silently normalised").
    const auto custom = calibrationLevel(100.0);
    CHECK(custom.operatorSupplied);
    CHECK(custom.nominalDb == 100.0);  // exactly what was passed, untouched

    CalibrationSession session;
    session.recordStartCheck(custom, sineAt1kHz(0.5, kFs), kFs, 0);
    REQUIRE(session.hasStartCheck());
    CHECK(session.startCheck().level.nominalDb == 100.0);
    CHECK(session.startCheck().level.operatorSupplied);
}

// --- A5: no invented refusal band ------------------------------------------

TEST_CASE("A5 no invented +-1.5 dB factory-calibration refusal is shipped here",
          "[calibration]") {
    // Record §8: 10EaZy refuses a calibration that differs more than +-1.5 dB
    // from ITS OWN factory reference, which this project does not have --
    // that is the shape of check only a flow (the start/end pair above) can
    // perform, not a constant to copy. codeText() strips comments and empties
    // literals, so this sentence's own "+-1.5 dB" in test prose cannot make
    // the grep pass by accident: it only ever reads the two SHIPPED files.
    for (const auto& file : {measureSrc() / "CalibrationSession.h",
                             measureSrc() / "CalibrationSession.cpp"}) {
        INFO("scanning " << file.string());
        const std::string code = rta::test::codeText(file);
        CHECK(code.find("1.5") == std::string::npos);
    }
}
