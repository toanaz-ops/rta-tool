// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L6a task W2-E2b part B (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md
// "W2-E -- the wiring nobody was assigned"; record docs/dsp/
// 2026-09-16-spl-pro-l6a.md §9, §15 A2): turning a session folder written by
// `SplLogWriter`/`writeCalibrationRecordFile` into a `ReportPayload`.
// JUCE-free -- real files on a temp directory, no AnalysisThread, no
// composition root.
#include "export/SplReportPayloadBuilder.h"

#include "export/SplCalibrationRecord.h"
#include "export/SplLog.h"
#include "SplReportPayloadBuilderTestSupport.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::measure::CalibrationLevel;
using rta::measure::CalibrationReportFields;
using rta::measure::CalibrationVerdict;
using rta::meter::Block;
using rta::splexport::SplCalibrationRecordInfo;
using rta::splexport::test::eightBlockFixture;
using rta::splexport::test::TempDir;
using rta::splexport::test::writeChannelLog;

TEST_CASE("buildReportPayload recomputes the whole-session Leq from sumSquares, offset applied",
         "[spl_report_payload_builder]") {
    TempDir dir("basic");
    constexpr double kOffset = 20.0;  // non-zero, distinguishable -- self-check's own requirement
    writeChannelLog(dir.path, 0, eightBlockFixture(), kOffset);

    rta::splexport::SplReportBuildRequest request;
    request.sessionDir = dir.path.string();
    request.channels = {0};

    const auto result = rta::splexport::buildReportPayload(request);
    REQUIRE(result.payload.has_value());
    CHECK(result.channelsWithNoLog.empty());
    REQUIRE(result.payload->metrics.size() == 1);

    // Record §3's own exact identity, closed-form: no exclusion, so every
    // block counts. Sigma sumSquares = 4*480*1 + 4*480*1e6; Sigma
    // blockSamples = 8*480.
    const double sigmaSumSquares = 4.0 * 480.0 * 1.0 + 4.0 * 480.0 * 1.0e6;
    const double sigmaSamples = 8.0 * 480.0;
    const double expectedLeq = 10.0 * std::log10(sigmaSumSquares / sigmaSamples) + kOffset;

    REQUIRE(result.payload->metrics[0].leqDb.has_value());
    CHECK_THAT(*result.payload->metrics[0].leqDb, WithinAbs(expectedLeq, 1e-9));
    CHECK_THAT(result.payload->sampleRate, WithinAbs(48000.0, 1e-9));
    CHECK(result.payload->config.calibrated);
    CHECK_THAT(result.payload->config.referenceOffsetDb, WithinAbs(kOffset, 1e-12));
    // No calibration record was written at all -- Wave 3's own cut fallback.
    CHECK_FALSE(result.payload->calibration.performed);
    CHECK(result.payload->validity.totalBlocks == 8);
    CHECK(result.payload->validity.excludedBlocks == 0);
    CHECK(result.payload->validity.bytesDiscarded == 0);
}

TEST_CASE("a FAILED calibration record brackets its own range CalibrationInvalid, "
         "excluding it from the recomputed Leq",
         "[spl_report_payload_builder]") {
    TempDir dir("bracket-fail");
    constexpr double kOffset = 20.0;
    writeChannelLog(dir.path, 0, eightBlockFixture(), kOffset);

    CalibrationReportFields fields;
    fields.performed = true;
    fields.start.level = CalibrationLevel{94.0, false};
    fields.start.measuredLevelDb = 10.0;
    fields.end.level = CalibrationLevel{94.0, false};
    fields.end.measuredLevelDb = 11.0;
    fields.driftDb = 1.0;  // > kMaxDriftDb -- Fail
    fields.verdict = CalibrationVerdict::Fail;
    fields.clause = rta::measure::CalibrationSession::kClause;

    SplCalibrationRecordInfo record;
    record.fields = fields;
    record.channel = 0;
    record.startBlockIndex = 3;
    record.endBlockIndex = 6;  // brackets exactly the four LOUD blocks
    rta::splexport::writeCalibrationRecordFile((dir.path / "calibration.txt").string(), record);

    rta::splexport::SplReportBuildRequest request;
    request.sessionDir = dir.path.string();
    request.channels = {0};

    const auto result = rta::splexport::buildReportPayload(request);
    REQUIRE(result.payload.has_value());
    REQUIRE(result.payload->metrics.size() == 1);

    // Only the four QUIET blocks (0,1,2,7) remain in membership: Sigma
    // sumSquares = 4*480*1, Sigma blockSamples = 4*480 -> ratio 1.0 ->
    // 10*log10(1) == 0 exactly, plus the offset.
    const double expectedLeq = 0.0 + kOffset;
    REQUIRE(result.payload->metrics[0].leqDb.has_value());
    CHECK_THAT(*result.payload->metrics[0].leqDb, WithinAbs(expectedLeq, 1e-9));
    CHECK(result.payload->validity.excludedBlocks == 4);
    CHECK(result.payload->calibration.performed);
    REQUIRE(result.payload->calibration.verdict.has_value());
    CHECK(*result.payload->calibration.verdict == CalibrationVerdict::Fail);
}

TEST_CASE("a PASSING calibration record brackets nothing -- every block still counts",
         "[spl_report_payload_builder]") {
    TempDir dir("bracket-pass");
    constexpr double kOffset = 20.0;
    writeChannelLog(dir.path, 0, eightBlockFixture(), kOffset);

    CalibrationReportFields fields;
    fields.performed = true;
    fields.start.level = CalibrationLevel{94.0, false};
    fields.end.level = CalibrationLevel{94.0, false};
    fields.driftDb = 0.5;  // exactly the boundary -- Pass (task self-check: "0.5 exact passes")
    fields.verdict = CalibrationVerdict::Pass;
    fields.clause = rta::measure::CalibrationSession::kClause;

    SplCalibrationRecordInfo record;
    record.fields = fields;
    record.channel = 0;
    record.startBlockIndex = 3;
    record.endBlockIndex = 6;
    rta::splexport::writeCalibrationRecordFile((dir.path / "calibration.txt").string(), record);

    rta::splexport::SplReportBuildRequest request;
    request.sessionDir = dir.path.string();
    request.channels = {0};

    const auto result = rta::splexport::buildReportPayload(request);
    REQUIRE(result.payload.has_value());
    REQUIRE(result.payload->metrics.size() == 1);

    const double sigmaSumSquares = 4.0 * 480.0 * 1.0 + 4.0 * 480.0 * 1.0e6;
    const double sigmaSamples = 8.0 * 480.0;
    const double expectedLeq = 10.0 * std::log10(sigmaSumSquares / sigmaSamples) + kOffset;
    REQUIRE(result.payload->metrics[0].leqDb.has_value());
    CHECK_THAT(*result.payload->metrics[0].leqDb, WithinAbs(expectedLeq, 1e-9));
    CHECK(result.payload->validity.excludedBlocks == 0);
}

// Fix round (2026-09-26), MEDIUM finding on item 15: `payload.
// calibrationOffsetApplied = calibrationRecord->offsetApplied;` had no test
// at all -- deleting that one line survived every one of the 578 cases in
// this suite. `record.offsetApplied` defaults to `true`
// (SplCalibrationRecord.h), so a test that never sets it (every other
// TEST_CASE above) cannot distinguish the mutant from the field's own
// default; this one sets BOTH values explicitly.
TEST_CASE("buildReportPayload copies calibrationOffsetApplied from the record, both ways",
         "[spl_report_payload_builder]") {
    constexpr double kOffset = 20.0;
    CalibrationReportFields fields;
    fields.performed = true;
    fields.start.level = CalibrationLevel{94.0, false};
    fields.end.level = CalibrationLevel{94.0, false};
    fields.driftDb = 0.0;
    fields.verdict = CalibrationVerdict::Pass;
    fields.clause = rta::measure::CalibrationSession::kClause;

    SECTION("offsetApplied = false") {
        TempDir dir("offset-applied-false");
        writeChannelLog(dir.path, 0, eightBlockFixture(), kOffset);

        SplCalibrationRecordInfo record;
        record.fields = fields;
        record.channel = 0;
        record.startBlockIndex = 3;
        record.endBlockIndex = 6;
        record.offsetApplied = false;
        rta::splexport::writeCalibrationRecordFile((dir.path / "calibration.txt").string(), record);

        rta::splexport::SplReportBuildRequest request;
        request.sessionDir = dir.path.string();
        request.channels = {0};

        const auto result = rta::splexport::buildReportPayload(request);
        REQUIRE(result.payload.has_value());
        CHECK_FALSE(result.payload->calibrationOffsetApplied);
    }

    SECTION("offsetApplied = true") {
        TempDir dir("offset-applied-true");
        writeChannelLog(dir.path, 0, eightBlockFixture(), kOffset);

        SplCalibrationRecordInfo record;
        record.fields = fields;
        record.channel = 0;
        record.startBlockIndex = 3;
        record.endBlockIndex = 6;
        record.offsetApplied = true;
        rta::splexport::writeCalibrationRecordFile((dir.path / "calibration.txt").string(), record);

        rta::splexport::SplReportBuildRequest request;
        request.sessionDir = dir.path.string();
        request.channels = {0};

        const auto result = rta::splexport::buildReportPayload(request);
        REQUIRE(result.payload.has_value());
        CHECK(result.payload->calibrationOffsetApplied);
    }
}

TEST_CASE("a calibration record measured on a DIFFERENT channel brackets nothing here",
         "[spl_report_payload_builder]") {
    TempDir dir("bracket-wrong-channel");
    constexpr double kOffset = 0.0;
    writeChannelLog(dir.path, 0, eightBlockFixture(), kOffset);

    CalibrationReportFields fields;
    fields.performed = true;
    fields.driftDb = 1.0;
    fields.verdict = CalibrationVerdict::Fail;
    fields.clause = rta::measure::CalibrationSession::kClause;

    SplCalibrationRecordInfo record;
    record.fields = fields;
    record.channel = 1;  // NOT channel 0, which is what this session logged
    record.startBlockIndex = 3;
    record.endBlockIndex = 6;
    rta::splexport::writeCalibrationRecordFile((dir.path / "calibration.txt").string(), record);

    rta::splexport::SplReportBuildRequest request;
    request.sessionDir = dir.path.string();
    request.channels = {0};

    const auto result = rta::splexport::buildReportPayload(request);
    REQUIRE(result.payload.has_value());
    CHECK(result.payload->validity.excludedBlocks == 0);  // the bracket is for channel 1, not 0
}

TEST_CASE("Ln/dose/alarm come from the live view, and their source is stated",
         "[spl_report_payload_builder]") {
    TempDir dir("live-view");
    writeChannelLog(dir.path, 0, eightBlockFixture(), 0.0);

    rta::splexport::SplReportBuildRequest request;
    request.sessionDir = dir.path.string();
    request.channels = {0};

    SECTION("no live view supplied") {
        const auto result = rta::splexport::buildReportPayload(request);
        REQUIRE(result.payload.has_value());
        CHECK_FALSE(result.payload->validity.lnDoseAlarmFromLiveSession);
        CHECK(result.payload->alarms.empty());
        CHECK_FALSE(result.payload->dose[0].percent.has_value());
    }

    SECTION("live view supplied") {
        rta::measure::SplBlockView live;
        live.dosePercent[0] = 12.5;
        live.doseProjected[1] = 30.0;
        live.lnDb[2] = 61.5;
        rta::measure::SplAlarmReading alarm;
        alarm.metricId = "leq_a_fast";
        alarm.limitDb = 100.0;
        alarm.state = rta::measure::SplAlarmState::Clear;
        live.alarms.push_back(alarm);
        request.liveView = live;

        const auto result = rta::splexport::buildReportPayload(request);
        REQUIRE(result.payload.has_value());
        CHECK(result.payload->validity.lnDoseAlarmFromLiveSession);
        REQUIRE(result.payload->alarms.size() == 1);
        CHECK(result.payload->alarms[0].metricId == "leq_a_fast");
        REQUIRE(result.payload->dose[0].percent.has_value());
        CHECK_THAT(*result.payload->dose[0].percent, WithinAbs(12.5, 1e-12));
        REQUIRE(result.payload->dose[1].projectedPercent.has_value());
        REQUIRE(!result.payload->metrics.empty());
        REQUIRE(result.payload->metrics[0].lnDb[2].has_value());
        CHECK_THAT(*result.payload->metrics[0].lnDb[2], WithinAbs(61.5, 1e-12));
    }
}

TEST_CASE("a channel with no readable log is counted, not silently dropped",
         "[spl_report_payload_builder]") {
    TempDir dir("missing-channel");
    writeChannelLog(dir.path, 0, eightBlockFixture(), 0.0);

    rta::splexport::SplReportBuildRequest request;
    request.sessionDir = dir.path.string();
    request.channels = {0, 5};  // channel 5 never logged anything

    const auto result = rta::splexport::buildReportPayload(request);
    REQUIRE(result.payload.has_value());  // channel 0 still built a payload
    REQUIRE(result.channelsWithNoLog.size() == 1);
    CHECK(result.channelsWithNoLog[0] == 5);
}

TEST_CASE("no channel has a readable log -- the payload itself is absent",
         "[spl_report_payload_builder]") {
    TempDir dir("no-log-at-all");
    // Nothing written to `dir.path` at all.

    rta::splexport::SplReportBuildRequest request;
    request.sessionDir = dir.path.string();
    request.channels = {0};

    const auto result = rta::splexport::buildReportPayload(request);
    CHECK_FALSE(result.payload.has_value());
    REQUIRE(result.channelsWithNoLog.size() == 1);
    CHECK(result.channelsWithNoLog[0] == 0);
}

TEST_CASE("a truncated final line is discarded and counted, never silently dropped",
         "[spl_report_payload_builder]") {
    TempDir dir("truncated");
    writeChannelLog(dir.path, 0, eightBlockFixture(), 0.0);

    // Append a partial, non-newline-terminated row directly to the segment
    // file -- the exact shape `SplLog.h::readLog`'s own C3 acceptance covers,
    // reached here through the payload builder rather than readLog directly.
    std::filesystem::path segment;
    for (const auto& entry : std::filesystem::directory_iterator(dir.path)) {
        if (entry.path().extension() == ".csv") segment = entry.path();
    }
    REQUIRE_FALSE(segment.empty());
    {
        std::ofstream out(segment, std::ios::app | std::ios::binary);
        out << "8,480,0,1.0e";  // deliberately truncated, no trailing newline
    }

    rta::splexport::SplReportBuildRequest request;
    request.sessionDir = dir.path.string();
    request.channels = {0};
    const auto result = rta::splexport::buildReportPayload(request);
    REQUIRE(result.payload.has_value());
    CHECK(result.payload->validity.bytesDiscarded > 0);
    CHECK(result.payload->validity.totalBlocks == 8);  // the eight WELL-FORMED rows still parsed
}

// Time history / marker cases (task W2-E2b fix round, MEDIUM finding) live in
// test_spl_report_payload_builder_fixes.cpp -- this file's own 400-line cap
// split; shared fixtures (TempDir/block/writeChannelLog/eightBlockFixture)
// moved to SplReportPayloadBuilderTestSupport.h so both files use one copy.
