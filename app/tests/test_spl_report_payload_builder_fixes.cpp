// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Task W2-E2b fix round (verifier MEDIUM finding): record §9 item 7's time
// history and markers, filled from the log's own per-block rows. Split out
// of test_spl_report_payload_builder.cpp to keep that file clear of the
// 400-line hard cap; shared fixtures live in
// SplReportPayloadBuilderTestSupport.h.
#include "export/SplReportPayloadBuilder.h"

#include "export/SplCalibrationRecord.h"
#include "SplReportPayloadBuilderTestSupport.h"

#include "rta/meter/Block.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

using Catch::Matchers::WithinAbs;

using rta::measure::CalibrationReportFields;
using rta::measure::CalibrationVerdict;
using rta::splexport::CalibrationRecordRefusal;
using rta::splexport::SplCalibrationRecordInfo;
using rta::splexport::test::eightBlockFixture;
using rta::splexport::test::TempDir;
using rta::splexport::test::writeChannelLog;

TEST_CASE("the time history has one point per block when nothing is excluded",
         "[spl_report_payload_builder]") {
    TempDir dir("history-full");
    writeChannelLog(dir.path, 0, eightBlockFixture(), 0.0);

    rta::splexport::SplReportBuildRequest request;
    request.sessionDir = dir.path.string();
    request.channels = {0};
    const auto result = rta::splexport::buildReportPayload(request);
    REQUIRE(result.payload.has_value());
    REQUIRE(result.payload->history.size() == 1);
    CHECK(result.payload->history[0].points.size() == 8);
}

TEST_CASE("the time history omits blocks a FAILED calibration record brackets",
         "[spl_report_payload_builder]") {
    // Same bracket-fail scenario as test_spl_report_payload_builder.cpp's own
    // Leq-exclusion test: blocks 3..6 are marked CalibrationInvalid in
    // memory and must not appear as trustworthy points on the strip, the
    // same "honoured the way the report already honours it" rule the
    // recomputed Leq follows.
    TempDir dir("history-excluded");
    writeChannelLog(dir.path, 0, eightBlockFixture(), 0.0);

    CalibrationReportFields fields;
    fields.performed = true;
    fields.driftDb = 1.0;
    fields.verdict = CalibrationVerdict::Fail;
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
    REQUIRE(result.payload->history.size() == 1);
    CHECK(result.payload->history[0].points.size() == 4);  // blocks 0,1,2,7 only
    // Fix round 4 (verifier LOW): SplReportPayloadBuilder.cpp:242-243's
    // `payload.validity.excludedBlockRange = ...` assignment had no test --
    // deleting it entirely would not have failed anything above, since the
    // history point COUNT is unaffected by whether the range is recorded.
    REQUIRE(result.payload->validity.excludedBlockRange.has_value());
    CHECK(result.payload->validity.excludedBlockRange->startBlockIndex == 3);
    CHECK(result.payload->validity.excludedBlockRange->endBlockIndex == 6);
}

// Fix round 4 (verifier LOW): SplReportPayloadBuilder.cpp:202's
// `payload.calibrationChannelRefusal = calibrationRecord->refusal;` had no
// test -- deleting it would render a refused record as "Drift 0.0 dB",
// silently claiming a comparison that never validly happened.
TEST_CASE("a refused calibration record carries its refusal reason into the payload",
         "[spl_report_payload_builder]") {
    TempDir dir("calibration-refused");
    writeChannelLog(dir.path, 0, eightBlockFixture(), 0.0);

    CalibrationReportFields fields;
    fields.performed = true;
    fields.clause = rta::measure::CalibrationSession::kClause;
    SplCalibrationRecordInfo record;
    record.fields = fields;
    record.refusal = CalibrationRecordRefusal::ChannelMismatch;
    rta::splexport::writeCalibrationRecordFile((dir.path / "calibration.txt").string(), record);

    rta::splexport::SplReportBuildRequest request;
    request.sessionDir = dir.path.string();
    request.channels = {0};
    const auto result = rta::splexport::buildReportPayload(request);
    REQUIRE(result.payload.has_value());
    CHECK(result.payload->calibrationChannelRefusal == CalibrationRecordRefusal::ChannelMismatch);
}

TEST_CASE("an Overload block flag produces an Overload marker", "[spl_report_payload_builder]") {
    TempDir dir("marker-overload");
    auto blocks = eightBlockFixture();
    blocks[2].flags |= rta::meter::flagMask(rta::meter::BlockFlag::Overload);
    writeChannelLog(dir.path, 0, blocks, 0.0);

    rta::splexport::SplReportBuildRequest request;
    request.sessionDir = dir.path.string();
    request.channels = {0};
    const auto result = rta::splexport::buildReportPayload(request);
    REQUIRE(result.payload.has_value());

    const auto& markers = result.payload->markers;
    const auto it = std::find_if(markers.begin(), markers.end(), [](const auto& m) {
        return m.kind == rta::measure::SplMarkerKind::Overload && m.blockIndex == 2;
    });
    REQUIRE(it != markers.end());
}

// Fix round 3 (verifier LOW, mutant M6 survived): valueDb's own formula
// (`appendHistoryAndMarkers`) adds `referenceOffsetDb`, but every prior
// fixture logged with offset 0.0, so dropping that term entirely would not
// have failed a single assertion. Block 0's raw mean-square is 1.0
// (eightBlockFixture's own comment), so its dB before any offset is exactly
// 0.0 -- a non-zero offset must appear in valueDb unchanged.
TEST_CASE("the time history's valueDb carries the log's own referenceOffsetDb",
         "[spl_report_payload_builder]") {
    TempDir dir("history-offset");
    constexpr double kOffsetDb = 12.3;
    writeChannelLog(dir.path, 0, eightBlockFixture(), kOffsetDb);

    rta::splexport::SplReportBuildRequest request;
    request.sessionDir = dir.path.string();
    request.channels = {0};
    const auto result = rta::splexport::buildReportPayload(request);
    REQUIRE(result.payload.has_value());
    REQUIRE(result.payload->history.size() == 1);
    REQUIRE_FALSE(result.payload->history[0].points.empty());
    CHECK_THAT(result.payload->history[0].points[0].valueDb, WithinAbs(kOffsetDb, 1e-9));
}

TEST_CASE("a Gap block flag produces a Gap marker", "[spl_report_payload_builder]") {
    TempDir dir("marker-gap");
    auto blocks = eightBlockFixture();
    blocks[5].flags |= rta::meter::flagMask(rta::meter::BlockFlag::Gap);
    writeChannelLog(dir.path, 0, blocks, 0.0);

    rta::splexport::SplReportBuildRequest request;
    request.sessionDir = dir.path.string();
    request.channels = {0};
    const auto result = rta::splexport::buildReportPayload(request);
    REQUIRE(result.payload.has_value());

    const auto& markers = result.payload->markers;
    const auto it = std::find_if(markers.begin(), markers.end(), [](const auto& m) {
        return m.kind == rta::measure::SplMarkerKind::Gap && m.blockIndex == 5;
    });
    REQUIRE(it != markers.end());
}
