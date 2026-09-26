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
#include "measure/SplConfig.h"
#include "measure/SplMeter.h"

#include "rta/dsp/Weighting.h"
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
using rta::measure::SplConfig;
using rta::measure::SplMeter;

namespace {

constexpr double kFs = 48000.0;

/// An `amplitude`-scaled sine at `frequencyHz`, `numSamples` long. The
/// caller picks `numSamples`/`frequencyHz` pairs that land on a whole number
/// of periods so the mean square is exact UP TO the float32 quantisation
/// `SplMeter::push` imposes on every sample (record §8's identity is proven
/// against exactly that floor, not a tighter one -- see A1 below).
std::vector<float> sineWave(double amplitude, double frequencyHz, double sampleRate,
                            std::size_t numSamples) {
    std::vector<float> x(numSamples);
    for (std::size_t n = 0; n < numSamples; ++n) {
        const double t = static_cast<double>(n) / sampleRate;
        x[n] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * frequencyHz * t));
    }
    return x;
}

/// The original fixture: one second at 1 kHz, 48 000 samples -- kept as its
/// own name because A2/A3/A4 only need SOME whole-period sine and do not
/// care about its length (A1 below is what tests the length itself).
std::vector<float> sineAt1kHz(double amplitude, double sampleRate) {
    return sineWave(amplitude, 1000.0, sampleRate, static_cast<std::size_t>(sampleRate));
}

std::filesystem::path measureSrc() {
    return std::filesystem::path(RTA_REPO_ROOT) / "app" / "src" / "measure";
}

std::filesystem::path appSrc() {
    return std::filesystem::path(RTA_REPO_ROOT) / "app" / "src";
}

}  // namespace

// --- A1: the closed form, no microphone ----------------------------------

TEST_CASE("A1 the closed form, no microphone: a sine through the Z path", "[calibration]") {
    // Record §8: L_meas = 10log10(A^2/2) = 20log10(A) - kFullScaleSineOffsetDb.
    // An identity, not a golden vector.
    //
    // Verifier round 1, finding 2: a mutant that hardcodes
    // measureRawZLevelDb's internal `blockSeconds` at 1.0 SURVIVED, because
    // every fixture in this file was exactly 48 000 samples at 48 kHz -- one
    // second, where the mutant's wrong constant and the correct
    // `samples.size()/sampleRate` computation happen to agree. The app itself
    // never feeds a round second (MainComponentCalibration.cpp's
    // kCalibrationCaptureLength is 8192), so two more fixtures are added
    // here, neither a round second, both a WHOLE number of periods so the
    // closed form still applies: 4800 samples @ 1 kHz is 100 cycles in 0.1 s,
    // and 8192 samples @ 1125 Hz is 192 cycles (1125 * 8192 / 48000 == 192
    // exactly -- 48000/gcd(8192,48000) == 375, and 1125 is a multiple of it).
    // Under the mutant, both non-1-second fixtures never close a block at all
    // (the accumulator waits for 48 000 samples that never arrive), so
    // `measureRawZLevelDb` returns the floor instead of the real level --
    // off by ~114 dB, nowhere near this test's 1e-6 tolerance.
    struct Fixture {
        const char* name;
        double frequencyHz;
        std::size_t numSamples;
    };
    const Fixture fixtures[] = {
        {"1 s @ 1 kHz (48000 samples)", 1000.0, 48000},
        {"0.1 s @ 1 kHz (4800 samples, 100 whole cycles)", 1000.0, 4800},
        {"8192 samples @ 1125 Hz (192 whole cycles; the app's own capture length)", 1125.0, 8192},
    };

    for (const auto& fixture : fixtures) {
        INFO("fixture: " << fixture.name);
        constexpr double amplitude = 0.5;
        const auto samples = sineWave(amplitude, fixture.frequencyHz, kFs, fixture.numSamples);
        const auto level = calibrationLevel(kIec60942Level94Db);

        CalibrationSession session;
        session.recordStartCheck(level, samples, kFs, 0);
        REQUIRE(session.hasStartCheck());

        const double expected = 20.0 * std::log10(amplitude) - kFullScaleSineOffsetDb;
        // TOLERANCE DEVIATION FROM THE PLAN'S 1e-12, MEASURED NOT ARGUED, same
        // shape as test_spl_seam.cpp's E2: CalibrationSession measures through
        // `SplMeter::push(std::span<const float>)`, so the sine is quantised
        // to float32 BEFORE the meter ever sees it. A float32 rounding is at
        // most half a ULP, 2^-24 relative to the sample; squaring roughly
        // doubles a relative error, so the mean square's worst-case relative
        // error is `2 * 2^-24`, and `10*log10(1 + 2*2^-24) = 5.18e-7 dB` is
        // the derived bound -- corrected from an earlier, wrong "~2.6e-7"
        // that dropped the factor of 2 from squaring. 1e-6 (comfortably
        // above 5.18e-7) is what this test asserts; it is unreachable to
        // tighten through the real Z path for any signal that is not exactly
        // float32-representable.
        INFO("residual = " << (session.startCheck().measuredLevelDb - expected));
        CHECK_THAT(session.startCheck().measuredLevelDb, WithinAbs(expected, 1e-6));

        // offset = L_cal - L_meas (record §8's one-line identity), and
        // applying it makes the same signal read L_cal -- the round trip
        // test_block.cpp's A7 already pins on calibrationOffsetDb itself,
        // checked here through the flow rather than the bare function.
        const double offset = session.referenceOffsetDb();
        CHECK(offset == level.nominalDb - session.startCheck().measuredLevelDb);
        CHECK_THAT(session.startCheck().measuredLevelDb + offset,
                  WithinAbs(level.nominalDb, 1e-9));
    }
}

TEST_CASE("A1b the offset, fed into a REAL session's SplMeter, reads L_cal exactly",
          "[calibration]") {
    // Verifier round 1, finding 4: A1's round-trip check above is pure
    // arithmetic on CalibrationSession's own stored fields -- it never
    // exercises the actual call a real session makes,
    // `SplMeter::blockLevelDb(block, meter.referenceOffsetDb())` against a
    // SECOND SplMeter built from an `SplConfig` carrying the computed
    // offset, which is what §8's "applying it" sentence promises. This
    // pushes the SAME samples through exactly that path.
    constexpr double amplitude = 0.5;
    const auto samples = sineAt1kHz(amplitude, kFs);
    const auto level = calibrationLevel(kIec60942Level94Db);

    CalibrationSession session;
    session.recordStartCheck(level, samples, kFs, 0);
    REQUIRE(session.hasStartCheck());

    SplConfig config;
    config.blockSeconds = static_cast<double>(samples.size()) / kFs;
    config.referenceOffsetDb = session.referenceOffsetDb();
    SplMeter meter(config, rta::dsp::WeightingType::Z, kFs);
    meter.push(samples);
    const auto block = meter.poll();
    REQUIRE(block.has_value());

    const double calibrated = SplMeter::blockLevelDb(*block, meter.referenceOffsetDb());
    // DERIVED, not the plan's bare "1e-12": measureRawZLevelDb is a pure,
    // deterministic function of `samples`, so the level this SECOND SplMeter
    // measures is the SAME double `m` A1 already computed. The only new
    // arithmetic is the round trip `m + (94.0 - m)`, two double roundings on
    // quantities of magnitude <= ~128 (the next power of 2 above 94): each
    // rounding is bounded by 2^-53 relative, i.e. ~128 * 2^-53 ~= 1.4e-14
    // absolute, so ~2.8e-14 for both -- comfortably inside 1e-12.
    INFO("residual = " << (calibrated - level.nominalDb));
    CHECK_THAT(calibrated, WithinAbs(level.nominalDb, 1e-12));
}

TEST_CASE("recordStartCheck/recordEndCheck refuse an empty span or a non-positive sample rate",
          "[calibration]") {
    // Verifier round 1, finding 3: the guard exists in both record*Check
    // methods but nothing exercised it -- a later edit could drop it with no
    // test noticing.
    const auto level = calibrationLevel(kIec60942Level94Db);
    const auto samples = sineAt1kHz(0.5, kFs);

    CalibrationSession session;
    session.recordStartCheck(level, {}, kFs, 0);
    CHECK_FALSE(session.hasStartCheck());
    session.recordStartCheck(level, samples, 0.0, 0);
    CHECK_FALSE(session.hasStartCheck());
    session.recordStartCheck(level, samples, -kFs, 0);
    CHECK_FALSE(session.hasStartCheck());

    // A good start check, then the same three refusals against END.
    session.recordStartCheck(level, samples, kFs, 0);
    REQUIRE(session.hasStartCheck());
    session.recordEndCheck(level, {}, kFs, 1000);
    CHECK_FALSE(session.hasEndCheck());
    session.recordEndCheck(level, samples, 0.0, 1000);
    CHECK_FALSE(session.hasEndCheck());
    session.recordEndCheck(level, samples, -kFs, 1000);
    CHECK_FALSE(session.hasEndCheck());
}

// LOW follow-up batch, item 17: a new START must clear any END check left
// over from a PREVIOUS bracket, or a stale pairing survives into the new one.
TEST_CASE("a new recordStartCheck clears the previous bracket's END check",
          "[calibration]") {
    const auto level = calibrationLevel(kIec60942Level94Db);
    const auto samples = sineAt1kHz(0.5, kFs);

    CalibrationSession session;
    session.recordStartCheck(level, samples, kFs, 1000);
    REQUIRE(session.hasStartCheck());
    session.recordEndCheck(level, samples, kFs, 2000);
    REQUIRE(session.hasEndCheck());
    REQUIRE(session.verdict().has_value());

    // START -> END -> START: the mutant this proves against is dropping the
    // `endCheck_.reset()` this fix adds. Without it, `hasEndCheck()`,
    // `driftDb()` and `verdict()` all kept reporting the FIRST bracket's
    // already-completed pairing after this second START, which is exactly
    // what a live readout (MainComponentCalibration.cpp's
    // updateCalibrationReadout) reads to decide what to show the operator.
    session.recordStartCheck(level, samples, kFs, 3000);
    REQUIRE(session.hasStartCheck());
    CHECK(session.startCheck().unixMs == 3000);
    CHECK_FALSE(session.hasEndCheck());
    CHECK_FALSE(session.driftDb().has_value());
    CHECK_FALSE(session.verdict().has_value());
    CHECK_FALSE(session.reportFields().performed);
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

// Verifier round 1, finding 1: mutants `<=`->`<`, 0.5->3.0 and 0.5->0.4 all
// SURVIVED against A2/A3 because neither fixture sits AT the boundary --
// 0.3 and 4.0 are both far enough from 0.5 that any of those three mutants
// still agrees with the correct answer. Pinning the boundary needs a
// fixture exactly there, which log10-derived drift cannot hit bit-exactly
// (§8's own quantities are transcendental) -- so the comparison itself is
// factored out and tested directly, against std::nextafter, with no
// measurement noise anywhere in the path.
TEST_CASE("A2b verdictForDrift is pinned at the exact 0.5 dB boundary", "[calibration]") {
    CHECK(CalibrationSession::kMaxDriftDb == 0.5);
    CHECK(CalibrationSession::verdictForDrift(0.5) == CalibrationVerdict::Pass);
    CHECK(CalibrationSession::verdictForDrift(std::nextafter(0.5, 1.0)) == CalibrationVerdict::Fail);
    // The other side, for completeness -- not itself a surviving mutant's
    // target, but the "iff" in A2's acceptance means both directions.
    CHECK(CalibrationSession::verdictForDrift(std::nextafter(0.5, 0.0)) == CalibrationVerdict::Pass);
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

// Verifier round 1, finding 5: `CalibrationReportFields::verdict` used to
// default-construct to `CalibrationVerdict::Pass`, a PLACEHOLDER for the
// absent case rather than an honest "no verdict yet"
// (memory/a-placeholder-for-an-absent-result-erases-its-state.md). A reader
// of `reportFields()` alone -- W4a's renderer, once it exists -- could not
// tell "calibration not performed" from "calibration passed" without ALSO
// checking `performed`. `verdict` is now `std::optional<CalibrationVerdict>`,
// mirroring `CalibrationSession::verdict()`'s own optionality exactly.
TEST_CASE("reportFields() does not invent a verdict before both checks exist", "[calibration]") {
    CalibrationSession session;
    auto fields = session.reportFields();
    CHECK_FALSE(fields.performed);
    CHECK_FALSE(fields.verdict.has_value());

    session.recordStartCheck(calibrationLevel(94.0), sineAt1kHz(0.5, kFs), kFs, 0);
    fields = session.reportFields();
    CHECK_FALSE(fields.performed);  // one check is still not a calibration
    CHECK_FALSE(fields.verdict.has_value());

    session.recordEndCheck(calibrationLevel(94.0), sineAt1kHz(0.5, kFs), kFs, 1000);
    fields = session.reportFields();
    CHECK(fields.performed);
    REQUIRE(fields.verdict.has_value());
    CHECK(*fields.verdict == CalibrationVerdict::Pass);
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
    // the grep pass by accident: it only ever reads the three SHIPPED files.
    //
    // Verifier round 1, finding 8: MainComponentCalibration.cpp (W3-B's
    // composition-root wiring) was not scanned, so a refusal band added
    // there instead of in CalibrationSession itself would have shipped
    // undetected.
    for (const auto& file : {measureSrc() / "CalibrationSession.h",
                             measureSrc() / "CalibrationSession.cpp",
                             appSrc() / "MainComponentCalibration.cpp"}) {
        INFO("scanning " << file.string());
        const std::string code = rta::test::codeText(file);
        CHECK(code.find("1.5") == std::string::npos);
    }
}
