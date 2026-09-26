// SPDX-License-Identifier: AGPL-3.0-or-later
//
// PR #32 fix round 3: the marker x-scale/excluded-range and
// calibration-channel-refusal findings. Split out of test_spl_report_fixes.cpp
// (rather than grown into it) to keep that file clear of the 400-line hard
// cap; shared fixtures live in SplReportTestSupport.h, same as that file.
#include "export/SplReport.h"
#include "SplReportTestSupport.h"
#include "view/Readouts.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace rta::splexport;
using rta::splexport::test::extractSection;
using rta::splexport::test::minimalPayload;

// Fix round 3 (verifier MEDIUM, the PR #28 bug class again): `first`/`last`
// used to come from `p.history` ALONE, but `appendHistoryAndMarkers` omits
// CalibrationInvalid blocks from history while still emitting Overload/Gap
// markers for EVERY block. The verifier's own probe: history spans blocks
// 0..1000 (a normal trace), but a Gap marker sits at block 1500 -- past the
// trace's own last point. The OLD scale used the history's own span alone,
// so this marker landed at (1500-0)/1000*1000 == 1500, outside the 0..1000
// viewBox entirely.
TEST_CASE("a marker beyond the history's own range stays inside the 0..1000 viewBox",
         "[spl_report]") {
    ReportPayload payload = minimalPayload();
    ReportHistorySeries series;
    series.metricId = "Main";
    series.points = {{0, 80.0}, {1000, 90.0}};
    payload.history = {series};

    rta::measure::SplMarker marker;
    marker.blockIndex = 1500;
    marker.kind = rta::measure::SplMarkerKind::Gap;
    payload.markers = {marker};

    const auto html = renderReport(payload);
    const auto history = extractSection(html, "history");

    // Folding the marker into first/last makes IT the new "last" (1500), so
    // it lands exactly at the right edge, and the trace's own former-last
    // point (1000) is correctly repositioned to 2/3 of the width -- neither
    // ends up at the pre-fix "1500.000000", which sat outside the viewBox.
    CHECK(history.find("x1=\"1000.000000\"") != std::string::npos);
    CHECK(history.find("x1=\"1500.000000\"") == std::string::npos);
}

// Fix round 3: after a FAILED calibration brackets [0, latest] (the common
// case -- restartSplLoggingForCalibration's own doc comment), history can be
// COMPLETELY EMPTY while markers still exist for every block. The OLD
// `if (!first) return 0.0;` collapsed every marker onto x=0, indistinguishable
// from each other.
TEST_CASE("an empty history with markers gives each marker a finite, distinct x",
         "[spl_report]") {
    ReportPayload payload = minimalPayload();
    payload.history.clear();

    rta::measure::SplMarker a, b;
    a.blockIndex = 10;
    a.kind = rta::measure::SplMarkerKind::Overload;
    b.blockIndex = 90;
    b.kind = rta::measure::SplMarkerKind::Gap;
    payload.markers = {a, b};

    const auto html = renderReport(payload);
    const auto history = extractSection(html, "history");

    CHECK(history.find("marker-overload") != std::string::npos);
    CHECK(history.find("marker-gap") != std::string::npos);
    // a is the new "first" (x=0), b is the new "last" (x=1000) -- distinct,
    // neither the pre-fix x=0 collapse both markers used to share.
    CHECK(history.find("x1=\"0.000000\"") != std::string::npos);
    CHECK(history.find("x1=\"1000.000000\"") != std::string::npos);
}

// Fix round 3: the excluded span is now shaded on the strip AND named in the
// Validity table -- a reader of either one alone still learns the prefix was
// dropped, and exactly which blocks.
TEST_CASE("the history strip shades the excluded block range", "[spl_report]") {
    ReportPayload payload = minimalPayload();
    ReportHistorySeries series;
    series.metricId = "Main";
    series.points = {{0, 80.0}, {10, 90.0}};
    payload.history = {series};
    payload.validity.excludedBlockRange = ReportValidity::ExcludedRange{0, 5};

    const auto html = renderReport(payload);
    CHECK(extractSection(html, "history").find("excluded-region") != std::string::npos);
}

TEST_CASE("the validity section states the excluded block range, or none", "[spl_report]") {
    ReportPayload withRange = minimalPayload();
    withRange.validity.excludedBlockRange = ReportValidity::ExcludedRange{3, 6};
    CHECK(extractSection(renderReport(withRange), "validity").find("3 .. 6") != std::string::npos);

    ReportPayload noRange = minimalPayload();  // excludedBlockRange absent
    CHECK(extractSection(renderReport(noRange), "validity").find(">none<") != std::string::npos);
}

// Fix round 3 (verifier MEDIUM, upgraded from LOW): a channel-mismatch (or
// no-measurement-channel) refusal must render its OWN reason, not a
// drift/verdict pair comparing two checks that were never comparable.
TEST_CASE("a channel-mismatch refusal renders instead of a drift/verdict", "[spl_report]") {
    ReportPayload payload = minimalPayload();
    payload.calibration.performed = true;
    payload.calibrationChannelRefusal = CalibrationRecordRefusal::ChannelMismatch;

    const auto html = renderReport(payload);
    const auto calibration = extractSection(html, "calibration");
    CHECK(calibration.find("Calibration refused") != std::string::npos);
    CHECK(calibration.find("DIFFERENT channels") != std::string::npos);
    CHECK(calibration.find("Drift") == std::string::npos);
}

TEST_CASE("a no-measurement-channel refusal states its own distinct reason", "[spl_report]") {
    ReportPayload payload = minimalPayload();
    payload.calibration.performed = true;
    payload.calibrationChannelRefusal = CalibrationRecordRefusal::NoMeasurementChannel;

    const auto calibration = extractSection(renderReport(payload), "calibration");
    CHECK(calibration.find("Calibration refused") != std::string::npos);
    CHECK(calibration.find("no measurement channel could be resolved") != std::string::npos);
}
