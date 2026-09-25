// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L6a task W4a-A / W3-C (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md sec.9, sec.11, sec.13 Q6/Q9).
#include "export/SplReport.h"
#include "export/SplReportScript.h"
#include "export/SplReportStyle.h"
#include "view/Readouts.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <regex>
#include <string>
#include <vector>

using namespace rta::splexport;
using rta::measure::calibrationLevel;
using rta::measure::CalibrationReportFields;
using rta::measure::CalibrationSession;
using rta::measure::CalibrationVerdict;

namespace {

ReportPayload minimalPayload() {
    ReportPayload p;
    p.venue = "Warehouse 9";
    p.event = "Load-in";
    p.appName = "rtatool";
    p.appVersion = "0.0.0";
    p.buildId = "test-build";
    p.device = "Test Device";
    p.channel = "Main L";
    p.sampleRate = 48000.0;
    return p;
}

ReportMetricResult mainMetric() {
    ReportMetricResult m;
    m.id = "Main";
    m.weighting = rta::dsp::WeightingType::A;
    m.detector = rta::meter::TimeWeighting::Fast;
    m.intervalSeconds = 900.0;  // 15 min
    m.leqDb = 85.34;
    m.lmaxDb = 95.0;
    m.lminDb = 60.0;
    m.lpeakDb = 110.0;
    m.selDb = 120.0;
    m.lnPercents = {1.0, 5.0, 10.0, 50.0, 90.0, 95.0};
    m.lnDb = {102.0, 98.0, 95.0, 88.0, 80.0, std::nullopt};
    return m;
}

CalibrationReportFields performedCalibration() {
    CalibrationReportFields f;
    f.performed = true;
    // 94.5 dB is not one of IEC 60942's own two nominal levels, so
    // CalibrationLevel::operatorSupplied is true -- exercising plan A4's
    // "any other value is accepted but recorded as operator-supplied".
    f.start.level = calibrationLevel(94.5);
    f.start.measuredLevelDb = -6.0;
    f.start.offsetDb = 100.0;
    f.start.unixMs = 1000;
    f.end.level = calibrationLevel(94.5);
    f.end.measuredLevelDb = -5.7;
    f.end.offsetDb = 100.3;
    f.end.unixMs = 7200000;
    f.driftDb = 0.3;
    f.verdict = CalibrationVerdict::Pass;
    f.clause = CalibrationSession::kClause;
    return f;
}

/// Every load-time and run-time fetch shape A1 names, defect 9's own list.
const std::vector<std::string>& forbiddenNetworkShapes() {
    static const std::vector<std::string> shapes = {
        "<script src=", "<link rel=\"stylesheet\"", "src=\"http", "@import", "url(http",
        "fetch(",       "XMLHttpRequest",           "navigator.sendBeacon", "EventSource",
        "WebSocket",    "import(",
    };
    return shapes;
}

std::string extractSection(const std::string& html, std::string_view id) {
    const auto start = html.find("id=\"" + std::string(id) + "\"");
    REQUIRE(start != std::string::npos);
    const auto end = html.find("</section>", start);
    REQUIRE(end != std::string::npos);
    return html.substr(start, end - start);
}

}  // namespace

// --- SHA-256 -------------------------------------------------------------

TEST_CASE("sha256Hex matches FIPS 180-4's own test vectors", "[spl_report]") {
    CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

// --- A1: self-contained, provably -----------------------------------------

TEST_CASE("A1 the frozen report contains no load-time or run-time network fetch",
         "[spl_report]") {
    const auto html = renderReport(minimalPayload());
    for (const auto& shape : forbiddenNetworkShapes()) {
        INFO("forbidden shape: " << shape);
        CHECK(html.find(shape) == std::string::npos);
    }
}

// --- A1b: inertness is TESTED, via the one seam that makes it real --------

TEST_CASE("A1b window.__SPL_PAYLOAD__ is present once in the report, absent from the shell",
         "[spl_report]") {
    const auto report = renderReport(minimalPayload());
    const auto shell = renderViewerShell();

    std::size_t count = 0;
    for (std::size_t pos = report.find("window.__SPL_PAYLOAD__ =");
        pos != std::string::npos;
        pos = report.find("window.__SPL_PAYLOAD__ =", pos + 1)) {
        ++count;
    }
    CHECK(count == 1);
    CHECK(shell.find("window.__SPL_PAYLOAD__") == std::string::npos);
}

// --- A2: the nine sections, each by a stable id ---------------------------

TEST_CASE("A2 all nine record sec.9 sections are present by id", "[spl_report]") {
    const auto html = renderReport(minimalPayload());
    for (std::string_view id : {"identification", "instrument", "calibration", "settings",
                                "results", "dose", "history", "validity", "integrity"}) {
        INFO("section id: " << id);
        CHECK(html.find("id=\"" + std::string(id) + "\"") != std::string::npos);
    }
}

// --- A3: the honesty sentence, no class claim -----------------------------

TEST_CASE("A3 the honesty sentence names Table 3, not Table 2, and makes no class claim",
         "[spl_report]") {
    const auto html = renderReport(minimalPayload());
    CHECK(html.find("0.05 dB") != std::string::npos);
    CHECK(html.find("IEC 61672-1:2013 Table 3") != std::string::npos);
    CHECK(html.find("not Table 2") != std::string::npos);
    CHECK(html.find("makes no claim of conformance to any IEC 61672-1 accuracy class") !=
         std::string::npos);
    CHECK(html.find("clause 3.28") != std::string::npos);

    // SPL-R10 / A8's own shape, run over the WHOLE document: "class" must
    // never be followed by (optional separator, then) the digit 0 or 1.
    static const std::regex kClassClaim("[Cc][Ll][Aa][Ss][Ss][ \t_-]*[01]");
    CHECK_FALSE(std::regex_search(html, kClassClaim));
}

// --- A4: it says what it does not claim -----------------------------------

TEST_CASE("A4 the report disclaims ISO 1996-2 clause 13 conformance", "[spl_report]") {
    const auto html = renderReport(minimalPayload());
    CHECK(html.find("ISO 1996-2:2017 clause 13") != std::string::npos);
    CHECK(html.find("market") != std::string::npos);
    CHECK(html.find("paywalled") != std::string::npos);
}

// --- A5: the integrity hash ------------------------------------------------

TEST_CASE("A5 the integrity hash reproduces bitwise and moves on one byte", "[spl_report]") {
    ReportPayload payload = minimalPayload();
    payload.logSegments = {"blockIndex,blockSamples\n0,48000\n", "1,48000\n"};

    const auto expected = sha256Hex(payload.logSegments[0] + payload.logSegments[1]);
    const auto html1 = renderReport(payload);
    const auto html2 = renderReport(payload);
    CHECK(html1.find(expected) != std::string::npos);
    CHECK(html1 == html2);  // same payload, bitwise identical rendering

    // Alter one byte of one segment.
    payload.logSegments[1][0] = '9';
    const auto mutatedExpected = sha256Hex(payload.logSegments[0] + payload.logSegments[1]);
    const auto html3 = renderReport(payload);
    CHECK(mutatedExpected != expected);
    CHECK(html3.find(mutatedExpected) != std::string::npos);
    CHECK(html3.find(expected) == std::string::npos);
}

// --- A6: Ln labels carry all four parts ------------------------------------

TEST_CASE("A6 Ln labels are the full ISO 1996-1 form, never a bare LNN", "[spl_report]") {
    ReportPayload payload = minimalPayload();
    payload.metrics = {mainMetric()};
    const auto html = renderReport(payload);

    CHECK(html.find("L_AF90,15min") != std::string::npos);
    CHECK(html.find("L_AF1,15min") != std::string::npos);

    static const std::regex kBareLn(R"(\bL[0-9])");
    CHECK_FALSE(std::regex_search(html, kBareLn));
}

// The absent Ln slot (L95, deliberately left std::nullopt above) must read
// as an absence with a reason, never as 0.0
// (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
TEST_CASE("A6b an absent Ln prints absent, never a placeholder zero", "[spl_report]") {
    ReportPayload payload = minimalPayload();
    payload.metrics = {mainMetric()};
    const auto html = renderReport(payload);
    const auto results = extractSection(html, "results");
    CHECK(results.find("L_AF95,15min") != std::string::npos);
    CHECK(results.find("absent") != std::string::npos);
}

// --- A7: rounding matches the desktop exactly ------------------------------

TEST_CASE("A7 every number is the C++ formatter's own output", "[spl_report]") {
    ReportPayload payload = minimalPayload();
    payload.sampleRate = 48000.4;
    payload.metrics = {mainMetric()};
    const auto html = renderReport(payload);

    CHECK(html.find(rta::view::formatHz(48000.4)) != std::string::npos);
    CHECK(html.find(rta::view::formatTrim(85.34)) != std::string::npos);
    // No fourth formatter: a raw two-decimal dB (the mistake a re-implementer
    // would make) must not appear anywhere near the Leq figure.
    CHECK(html.find("85.34 dB") == std::string::npos);
}

// --- A8: no CSS class trips the honesty guard ------------------------------

TEST_CASE("A8 SplReportStyle.h and SplReportScript.h never spell class+digit",
         "[spl_report]") {
    static const std::regex kClassClaim("[Cc][Ll][Aa][Ss][Ss][ \t_-]*[01]");
    CHECK_FALSE(std::regex_search(std::string(kReportStyle), kClassClaim));
    CHECK_FALSE(std::regex_search(std::string(kReportScript), kClassClaim));
}

// --- report byte size, measured not assumed --------------------------------

TEST_CASE("the rendered report stays small even over a long session", "[spl_report]") {
    ReportPayload payload = minimalPayload();
    ReportHistorySeries series;
    series.metricId = "Main";
    for (std::uint64_t i = 0; i < 28800; ++i) {  // 8 h at 1 s blocks
        series.points.push_back({i, 80.0 + static_cast<double>(i % 20)});
    }
    payload.history = {series};
    const auto html = renderReport(payload);
    CHECK(html.size() < 3'000'000);
}

// --- W3-C: calibration in the report ---------------------------------------

TEST_CASE("W3-C not performed prints the fallback sentence and never a verdict",
         "[spl_report]") {
    ReportPayload payload = minimalPayload();
    payload.calibration = CalibrationReportFields{};  // performed == false
    const auto html = renderReport(payload);
    const auto calibration = extractSection(html, "calibration");
    CHECK(calibration.find("calibration check not performed") != std::string::npos);
    // The mutation the plan prescribes: "print Pass when not performed" ->
    // this must go RED against it.
    CHECK(calibration.find("Pass") == std::string::npos);
    CHECK(calibration.find("Fail") == std::string::npos);
}

TEST_CASE("W3-C performed prints the pair, the drift, the nominal and the clause",
         "[spl_report]") {
    ReportPayload payload = minimalPayload();
    payload.calibration = performedCalibration();
    const auto html = renderReport(payload);
    const auto calibration = extractSection(html, "calibration");

    CHECK(calibration.find(rta::view::formatTrim(-6.0)) != std::string::npos);
    CHECK(calibration.find(rta::view::formatTrim(-5.7)) != std::string::npos);
    CHECK(calibration.find(rta::view::formatTrim(0.3)) != std::string::npos);
    CHECK(calibration.find(rta::view::formatTrim(94.5)) != std::string::npos);
    CHECK(calibration.find("operator-supplied") != std::string::npos);
    CHECK(calibration.find("ISO 1996-2:2017 cl. 5.2") != std::string::npos);
    CHECK(calibration.find("Pass") != std::string::npos);
    CHECK(calibration.find("1000") != std::string::npos);
    CHECK(calibration.find("7200000") != std::string::npos);
}
