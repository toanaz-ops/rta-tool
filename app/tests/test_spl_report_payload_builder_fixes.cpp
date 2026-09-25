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

#include <algorithm>
#include <cstdint>
#include <vector>

using rta::measure::CalibrationReportFields;
using rta::measure::CalibrationVerdict;
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
