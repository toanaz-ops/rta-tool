// SPDX-License-Identifier: AGPL-3.0-or-later
//
// PR #28 fix round: cases for the independent verifier's six HIGH/MEDIUM
// findings. Split out of test_spl_report.cpp to keep that file clear of
// the 400-line hard cap; shared fixtures live in SplReportTestSupport.h.
#include "export/SplReport.h"
#include "SplReportTestSupport.h"
#include "view/Readouts.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

using namespace rta::splexport;
using rta::splexport::test::extractSection;
using rta::splexport::test::minimalPayload;

// Fix round (PR #28 verifier, HIGH): the record sec.12 constraint-2
// "untested for the viewer" sentence lived only in renderViewerShell(),
// which the product never emits -- the owner decision says that sentence
// goes in the REPORT.
TEST_CASE("the frozen report itself states the viewer rounding constraint is untested",
         "[spl_report]") {
    const auto html = renderReport(minimalPayload());
    CHECK(html.find("untested for the viewer") != std::string::npos);
}

// Fix round (PR #28 verifier, MEDIUM): record sec.9 item 4 wants "dose
// preset with L_c, T_c, q and threshold" in Settings, and none of the
// four appeared. `minimalPayload()`'s default-constructed `config.dose`
// already carries the real NIOSH REL / OSHA PEL presets
// (SplConfig::dose's own default), so this needs no fixture beyond that.
TEST_CASE("Settings prints each dose preset's L_c, q and threshold", "[spl_report]") {
    const auto html = renderReport(minimalPayload());
    const auto settings = extractSection(html, "settings");
    // COPIES, not references: `ReportPayload{}` is a temporary, and binding
    // a reference to one of its array elements dangles the moment the full
    // expression ends (gcc -Wdangling-reference/-Wdangling-pointer,
    // AppleClang -Wdangling-gsl -- CI's warning gate on both, and lines
    // below would have read freed stack).
    const auto niosh = ReportPayload{}.config.dose[0];  // 85.0 dB, q=9.9657843..., 80.0 dB
    const auto osha = ReportPayload{}.config.dose[1];   // 90.0 dB, q=16.6096405..., 90.0 dB
    CHECK(settings.find(rta::view::formatTrim(niosh.criterionLevelDb)) != std::string::npos);
    CHECK(settings.find(rta::view::formatTrim(niosh.thresholdDb)) != std::string::npos);
    CHECK(settings.find(rta::view::formatTrim(osha.criterionLevelDb)) != std::string::npos);
    CHECK(settings.find(rta::view::formatTrim(osha.thresholdDb)) != std::string::npos);
    // q is a dimensionless exchange-rate denominator, not a dB value --
    // rendered as a plain number, never through formatTrim (which would
    // print a false "dB" unit on it). Round-3 fix: one decimal printed
    // NIOSH's 9.9657843 as "10.0", textually the SAME as the q=10 value
    // record sec.7 spends a page telling apart (SplConfig.h's own
    // exchangeDenominator comment: the two constants differ by 1.53e-08
    // relative, but the whole point of using the computed value instead of
    // a literal is that it is NOT 10). Seven decimals, matching the
    // record's own table, cannot collide the two.
    CHECK(settings.find("9.9657843</td>") != std::string::npos);
    CHECK(settings.find("16.6096405</td>") != std::string::npos);
    CHECK(settings.find("10.0000000</td>") == std::string::npos);
}

// Fix round (PR #28 verifier, MEDIUM): ReportDoseResult's percent/
// projectedPercent/twaDb/exposureLevel8hDb had no optionals, so a preset
// with nothing accumulated yet rendered "0.0 % ... 0.0 dB" -- indistinguishable
// from a preset that measured exactly zero dose
// (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
TEST_CASE("a dose preset with nothing accumulated prints absent, never 0.0",
         "[spl_report]") {
    ReportPayload payload = minimalPayload();
    payload.dose[0].label = "NIOSH REL";
    // percent/projectedPercent/twaDb/exposureLevel8hDb all left absent.
    const auto html = renderReport(payload);
    const auto dose = extractSection(html, "dose");
    CHECK(dose.find("NIOSH REL") != std::string::npos);
    CHECK(dose.find("absent") != std::string::npos);
    CHECK(dose.find("0.0 %") == std::string::npos);
    CHECK(dose.find("0.0 dB") == std::string::npos);
}

// Fix round (PR #28 verifier, MEDIUM): the payload's SplAlarmState never
// reached the rendered report at all. Filling gets record sec.15 A6's own
// words -- it is NOT Clear, because Clear would claim "compared, and
// under the limit" for a comparison that never ran
// (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
TEST_CASE("alarm state is rendered for all three states, with the existing state-* classes",
         "[spl_report]") {
    ReportPayload payload = minimalPayload();
    ReportAlarmResult filling;
    filling.metricId = "Main";
    filling.limitDb = 100.0;
    filling.windowBlocks = 900;
    filling.state = rta::measure::SplAlarmState::Filling;
    ReportAlarmResult clear = filling;
    clear.metricId = "Clear metric";
    clear.state = rta::measure::SplAlarmState::Clear;
    ReportAlarmResult fired = filling;
    fired.metricId = "Fired metric";
    fired.state = rta::measure::SplAlarmState::Fired;
    payload.alarms = {filling, clear, fired};

    const auto html = renderReport(payload);
    const auto settings = extractSection(html, "settings");
    // Round-3 fix: searching the whole document for "state-fired" and
    // ">Fired<" SEPARATELY is vacuous -- SplReportStyle.h's CSS always
    // contains all three `.state-*` selectors regardless of what the
    // table actually rendered, and mutants M11 (Fired rendered with the
    // state-clear class) and M06 (Filling's own label swapped for
    // "Clear") both survived that shape. Assert the full CELL instead, so
    // the class and the label are pinned to the SAME element.
    CHECK(settings.find(R"(class="state-filling">Filling -- window not yet full, not compared<)") !=
         std::string::npos);
    CHECK(settings.find(R"(class="state-clear">Clear<)") != std::string::npos);
    CHECK(settings.find(R"(class="state-fired">Fired<)") != std::string::npos);
}

// Fix round (PR #28 verifier, MEDIUM): mutant M11 (leaving `<` unescaped in
// escapeHtml) survived the whole suite -- every payload string field IS
// escaped in the source, but nothing exercised a string containing markup,
// so nothing could have caught its removal.
TEST_CASE("every escaped payload field defeats an embedded script tag", "[spl_report]") {
    ReportPayload payload = minimalPayload();
    const std::string xss = "<script>alert(1)</script>";
    payload.notes = xss;
    payload.config.alarms = {{xss, 100.0, 60}};
    payload.validity.segmentPaths = {xss};

    const auto html = renderReport(payload);
    CHECK(html.find("&lt;script&gt;") != std::string::npos);
    CHECK(html.find("<script>alert") == std::string::npos);
}

// Fix round (PR #28 verifier, MEDIUM): the marker x-scale must match the
// trace's own -- both walk the SAME (idx-first)/span*width mapping, or an
// alarm marker drawn on a >1000-block session lands nowhere near the
// trace point it is supposed to annotate (28799 % 1000 == 799, nowhere
// near the trace's own last-point x of 1000).
TEST_CASE("a marker at the last block lands at the same x as the trace's last point",
         "[spl_report]") {
    ReportPayload payload = minimalPayload();
    ReportHistorySeries series;
    series.metricId = "Main";
    series.points = {{0, 80.0}, {28799, 90.0}};  // longer than 1000 blocks
    payload.history = {series};

    rta::measure::SplMarker marker;
    marker.blockIndex = 28799;
    marker.kind = rta::measure::SplMarkerKind::Alarm;
    payload.markers = {marker};

    const auto html = renderReport(payload);
    const auto history = extractSection(html, "history");

    // Both the trace's last point and the marker sit at exactly the
    // viewBox's right edge (idx == last, so (idx-first)/span*1000 == 1000)
    // -- std::to_string(1000.0) is deterministic, so the same literal text
    // must appear in both places.
    const std::string lastPointX = "1000.000000,";
    const std::string markerX = "x1=\"1000.000000\"";
    CHECK(history.find(lastPointX) != std::string::npos);
    CHECK(history.find(markerX) != std::string::npos);
    // Mutant M14 (drop markers entirely) must go RED against this: no
    // marker line at all means no x1 attribute of any kind.
    CHECK(history.find("marker-alarm") != std::string::npos);
}

// Fix round (PR #28 round-3 fix, LOW, M12): a Filling alarm has not yet
// compared anything, so its headroomDb is absent (SplAlarmReading's own
// contract, test_spl_alarms.cpp's B4/closed-form(c) cases) -- but nothing in
// this suite ever asserted what the REPORT does with that absence. Mutant
// M12 (`dbOrAbsent(a.headroomDb)` -> `escapeHtml(formatTrim(a.headroomDb
// .value_or(0.0)))`) survived: it renders a silent "0.0 dB", indistinguishable
// from a real zero-headroom reading
// (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
TEST_CASE("an alarm with no headroom reading renders absent, never 0.0 dB",
         "[spl_report]") {
    ReportPayload payload = minimalPayload();
    ReportAlarmResult filling;
    filling.metricId = "Main";
    filling.limitDb = 100.0;
    filling.windowBlocks = 900;
    filling.state = rta::measure::SplAlarmState::Filling;
    // headroomDb left default-constructed: std::optional's empty state.
    payload.alarms = {filling};

    const auto html = renderReport(payload);
    const auto settings = extractSection(html, "settings");
    // A generic "0.0 dB" substring search is unsafe here: the OSHA dose
    // preset's own criterion/threshold ("90.0 dB") CONTAINS the literal
    // "0.0 dB" starting at its second character, and that preset always
    // renders a few rows below the alarm-status table (fix 2 above). Assert
    // the full headroom CELL instead, pinned to the one row this fixture
    // creates -- the same shape fix 3 above uses for the state cell.
    CHECK(settings.find(
              R"(Filling -- window not yet full, not compared</td><td><span class="absent">absent (no data)</span></td>)") !=
         std::string::npos);
}

// Fix round (PR #28 round-3 fix, pre-existing defect): `cfg.blockSeconds`
// is a duration in seconds, not a sound level, but Settings printed it
// through `formatTrim` -- the dB formatter -- so the default 1.0 s block
// interval read "1.0 dB" in the report. `intervalText` is not the right
// fix either: it rounds to the nearest WHOLE second (`static_cast<long
// long>(seconds + 0.5)`), which would silently floor a sub-second block
// interval like 0.125 s to "0s". Block interval needs its own formatter:
// decimal precision, "s" unit, never dB.
TEST_CASE("Settings prints the block interval in seconds, not decibels",
         "[spl_report]") {
    ReportPayload payload = minimalPayload();
    payload.config.blockSeconds = 1.0;  // SplConfig's own default
    const auto html = renderReport(payload);
    const auto settings = extractSection(html, "settings");
    CHECK(settings.find(
              R"(<span class="label">Block interval</span><span class="value">1.0 s</span>)") !=
         std::string::npos);
    CHECK(settings.find("1.0 dB") == std::string::npos);
}

// Fix round (PR #28 round-3 fix, LOW, M09): a single-block session has
// `first == last`, so the span computation's `*last > *first` branch is
// false and the fallback value is what actually divides every x. Changing
// that fallback from 1.0 to 0.0 went unnoticed by every other fixture
// (all of which span more than one block) -- 0.0/0.0 is `nan`, and a
// nonzero delta over a zero span is `inf`. This session has exactly one
// block, so it is the one fixture that exercises the fallback at all.
TEST_CASE("a single-block session renders finite x coordinates, no nan or inf",
         "[spl_report]") {
    ReportPayload payload = minimalPayload();
    ReportHistorySeries series;
    series.metricId = "Main";
    series.points = {{42, 80.0}};  // exactly one block: first == last
    payload.history = {series};

    rta::measure::SplMarker marker;
    marker.blockIndex = 42;
    marker.kind = rta::measure::SplMarkerKind::Alarm;
    payload.markers = {marker};

    const auto html = renderReport(payload);
    const auto history = extractSection(html, "history");
    CHECK(history.find("nan") == std::string::npos);
    CHECK(history.find("inf") == std::string::npos);
    // The one point and the one marker both sit at x=0 (delta 0 over any
    // finite, nonzero span).
    CHECK(history.find("0.000000,") != std::string::npos);
    CHECK(history.find("x1=\"0.000000\"") != std::string::npos);
}
