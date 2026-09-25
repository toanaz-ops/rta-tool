// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L6a task W2-E2b part A (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md
// "W2-E -- the wiring nobody was assigned"; record docs/dsp/
// 2026-09-16-spl-pro-l6a.md §8, §9 item 3, §15 A2): the calibration record
// written into a session folder once the END check completes, round-tripped
// through `parseCalibrationRecord`. JUCE-free -- pure string/file I/O, no
// AnalysisThread and no composition root in the path.
#include "export/SplCalibrationRecord.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

using Catch::Matchers::WithinAbs;
using rta::measure::CalibrationLevel;
using rta::measure::CalibrationReportFields;
using rta::measure::CalibrationVerdict;
using rta::splexport::calibrationRecordText;
using rta::splexport::parseCalibrationRecord;
using rta::splexport::SplCalibrationRecordInfo;
using rta::splexport::writeCalibrationRecordFile;

namespace {

/// A fully-populated, PASSING record -- drift exactly at the boundary
/// (0.5 dB, task self-check's own "drift 0.5 exact passes" case), distinct
/// start/end levels and unix times so a field mix-up (start read as end, or
/// vice versa) fails the round-trip rather than passing by symmetry.
SplCalibrationRecordInfo passingRecord() {
    CalibrationReportFields fields;
    fields.performed = true;
    fields.start.level = CalibrationLevel{94.0, false};
    fields.start.measuredLevelDb = 12.5;
    fields.start.offsetDb = 81.5;
    fields.start.unixMs = 1'700'000'000'000ull;
    fields.end.level = CalibrationLevel{94.0, false};
    fields.end.measuredLevelDb = 13.0;
    fields.end.offsetDb = 81.0;
    fields.end.unixMs = 1'700'003'600'000ull;
    fields.driftDb = 0.5;  // exactly CalibrationSession::kMaxDriftDb
    fields.verdict = rta::measure::CalibrationSession::verdictForDrift(fields.driftDb);
    fields.clause = rta::measure::CalibrationSession::kClause;

    SplCalibrationRecordInfo info;
    info.fields = fields;
    info.channel = 0;
    info.startBlockIndex = 0;
    info.endBlockIndex = 3599;
    return info;
}

}  // namespace

TEST_CASE("calibrationRecordText round-trips every field through parseCalibrationRecord",
         "[spl_calibration_record]") {
    const auto info = passingRecord();
    const auto text = calibrationRecordText(info);

    const auto parsed = parseCalibrationRecord(text);
    REQUIRE(parsed.has_value());
    CHECK(parsed->fields.performed);
    CHECK(parsed->channel == info.channel);
    CHECK(parsed->startBlockIndex == info.startBlockIndex);
    CHECK(parsed->endBlockIndex == info.endBlockIndex);
    CHECK_THAT(parsed->fields.start.measuredLevelDb, WithinAbs(info.fields.start.measuredLevelDb, 1e-12));
    CHECK_THAT(parsed->fields.start.level.nominalDb, WithinAbs(info.fields.start.level.nominalDb, 1e-12));
    CHECK(parsed->fields.start.level.operatorSupplied == info.fields.start.level.operatorSupplied);
    CHECK(parsed->fields.start.unixMs == info.fields.start.unixMs);
    CHECK_THAT(parsed->fields.end.measuredLevelDb, WithinAbs(info.fields.end.measuredLevelDb, 1e-12));
    CHECK(parsed->fields.end.unixMs == info.fields.end.unixMs);
    CHECK_THAT(parsed->fields.driftDb, WithinAbs(info.fields.driftDb, 1e-12));
    REQUIRE(parsed->fields.verdict.has_value());
    CHECK(*parsed->fields.verdict == CalibrationVerdict::Pass);
    // `clause` is not read back from the file (SplCalibrationRecord.h's own
    // doc comment: no static-storage view can be reconstructed from bytes on
    // disk) -- it is set directly to the one clause constant this project
    // has, so a successful parse must still carry it.
    CHECK(parsed->fields.clause == rta::measure::CalibrationSession::kClause);
}

TEST_CASE("driftDb exactly at nextafter(0.5) parses back as Fail, not Pass",
         "[spl_calibration_record]") {
    // Task self-check's own "nextafter fails through the whole path" case,
    // exercised at the record layer: the ONE bit that decides bracket
    // exclusion in the report payload builder is `verdict == Fail`, so this
    // is the boundary that matters for THIS file, not CalibrationSession's
    // own arithmetic (already proven in test_calibration.cpp).
    auto info = passingRecord();
    info.fields.driftDb = std::nextafter(0.5, 1.0);
    info.fields.verdict = rta::measure::CalibrationSession::verdictForDrift(info.fields.driftDb);
    REQUIRE(info.fields.verdict == CalibrationVerdict::Fail);

    const auto parsed = parseCalibrationRecord(calibrationRecordText(info));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->fields.verdict.has_value());
    CHECK(*parsed->fields.verdict == CalibrationVerdict::Fail);
}

TEST_CASE("performed == false writes a one-line record, not an empty file",
         "[spl_calibration_record]") {
    SplCalibrationRecordInfo info;  // default: performed == false
    const auto text = calibrationRecordText(info);
    CHECK_FALSE(text.empty());
    CHECK(text.find("calibrationPerformed=0") != std::string::npos);

    const auto parsed = parseCalibrationRecord(text);
    REQUIRE(parsed.has_value());
    CHECK_FALSE(parsed->fields.performed);
}

TEST_CASE("a record missing the required first line parses as absent, not half-filled",
         "[spl_calibration_record]") {
    // No atomicity claim, the SplLog.h::readLog precedent: garbage in is
    // std::nullopt, never a struct with some fields real and others default.
    CHECK_FALSE(parseCalibrationRecord("").has_value());
    CHECK_FALSE(parseCalibrationRecord("# someOtherKey=1\n").has_value());
}

TEST_CASE("writeCalibrationRecordFile writes bytes readWholeFile/parseCalibrationRecord agree on",
         "[spl_calibration_record]") {
    const auto dir = std::filesystem::temp_directory_path() / "rta-test-splcalibrecord";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto path = (dir / "calibration.txt").string();

    const auto info = passingRecord();
    writeCalibrationRecordFile(path, info);

    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    const auto parsed = parseCalibrationRecord(ss.str());
    REQUIRE(parsed.has_value());
    CHECK(parsed->endBlockIndex == info.endBlockIndex);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
