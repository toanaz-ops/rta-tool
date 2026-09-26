// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L6a task W2-E2b part B: the inverse of SplLog.h's `logHeader()`,
// needed by the report payload builder to recover `referenceOffsetDb` from a
// log file that has already been written. JUCE-free.
#include "export/SplLogHeaderParse.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using rta::splexport::parseLogHeader;
using rta::splexport::SplLogHeaderInfo;

TEST_CASE("parseLogHeader is the exact inverse of logHeader for a calibrated log",
         "[spl_log_header_parse]") {
    rta::measure::SplConfig config;
    config.referenceOffsetDb = 81.5;  // distinguishable, non-zero -- self-check's own requirement
    config.calibrated = true;

    SplLogHeaderInfo info;
    info.weighting = rta::dsp::WeightingType::A;
    info.detector = rta::meter::TimeWeighting::Fast;
    info.blockSamples = 480;
    info.sampleRate = 48000.0;
    info.startedAtUnixMs = 1'700'000'000'000ull;
    info.calibratorLevelDb = 94.0;

    std::string body = rta::splexport::logHeader(config, info);
    body += std::string(rta::splexport::csvHeaderRow()) + "\n";
    body += "0,480,0,1.0e+02,10.0,10.0,10.0,10.0,0\n";  // one data row, ignored by the header parse

    const auto parsed = parseLogHeader(body);
    REQUIRE(parsed.has_value());
    CHECK_THAT(parsed->referenceOffsetDb, WithinAbs(config.referenceOffsetDb, 1e-12));
    CHECK(parsed->calibrated == config.calibrated);
    CHECK(parsed->info.weighting == info.weighting);
    CHECK(parsed->info.detector == info.detector);
    CHECK(parsed->info.blockSamples == info.blockSamples);
    CHECK_THAT(parsed->info.sampleRate, WithinAbs(info.sampleRate, 1e-12));
    REQUIRE(parsed->info.calibratorLevelDb.has_value());
    CHECK_THAT(*parsed->info.calibratorLevelDb, WithinAbs(*info.calibratorLevelDb, 1e-12));
}

TEST_CASE("parseLogHeader reports uncalibrated -- dbfs, no calibratorLevelDb",
         "[spl_log_header_parse]") {
    rta::measure::SplConfig config;  // referenceOffsetDb = 0.0, calibrated = false
    SplLogHeaderInfo info;
    info.weighting = rta::dsp::WeightingType::C;
    info.detector = rta::meter::TimeWeighting::Slow;
    info.blockSamples = 48000;
    info.sampleRate = 48000.0;

    const auto body = rta::splexport::logHeader(config, info) + std::string(rta::splexport::csvHeaderRow());
    const auto parsed = parseLogHeader(body);
    REQUIRE(parsed.has_value());
    CHECK_FALSE(parsed->calibrated);
    CHECK_THAT(parsed->referenceOffsetDb, WithinAbs(0.0, 1e-12));
    CHECK_FALSE(parsed->info.calibratorLevelDb.has_value());
    CHECK(parsed->info.weighting == rta::dsp::WeightingType::C);
    CHECK(parsed->info.detector == rta::meter::TimeWeighting::Slow);
}

TEST_CASE("parseLogHeader returns absent when a required key is missing",
         "[spl_log_header_parse]") {
    // No atomicity claim, the readLog precedent: a header missing
    // calibrationOffsetDb (never written, or truncated mid-write) must not
    // silently default to 0.0 -- that would be indistinguishable from a REAL
    // 0.0 dB offset.
    const std::string body = "# schema=1\n# weighting=A\n# detector=Fast\nblockIndex,...\n";
    CHECK_FALSE(parseLogHeader(body).has_value());
}

TEST_CASE("parseLogHeader returns absent when calibrationOffsetDb ALONE is missing",
         "[spl_log_header_parse]") {
    // The case above is missing every key at once (weighting/detector AND
    // the offset), so it cannot tell whether `sawOffset` is actually load-
    // bearing in the final guard -- a mutant that drops `|| !sawOffset`
    // from that guard still returns absent there because the other three
    // checks still fail. This fixture supplies every OTHER required key and
    // omits only calibrationOffsetDb, so it fails for exactly one reason:
    // measured red when `sawOffset` is dropped from the guard (mutation
    // testing, task W2-E2b part B mutant d).
    const std::string body =
        "# schema=1\n# startedAtUnixMs=1700000000000\n# sampleRate=48000\n"
        "# blockSamples=480\n# weighting=A\n# detector=Fast\nblockIndex,...\n";
    CHECK_FALSE(parseLogHeader(body).has_value());
}
