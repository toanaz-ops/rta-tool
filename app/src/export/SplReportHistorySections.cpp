// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
// Lane L6a task W4a-A / W3-C (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md sec.9, sec.11).
//
// Split out of SplReportSections.cpp (fix round 3) so that file stays under
// the 400-line hard cap once the marker/excluded-range and calibration-
// refusal fixes below grew it. `renderCalibration`, `renderHistory` and
// `renderValidity` -- the three record sec.9 sections a calibration-channel
// refusal (SplCalibrationRecord.h's own `CalibrationRecordRefusal`) touches
// -- live together here; the other six section renderers stay in
// SplReportSections.cpp. Declared in the SAME SplReportSections.h; this file
// only supplies their definitions.
#include "export/SplReportSections.h"

#include "export/SplReportStyle.h"
#include "view/Readouts.h"

#include <algorithm>

namespace rta::splexport::detail {

using rta::view::formatTrim;

// W3-C: record sec.9 item 3. `performed` gates first -- `verdict` is
// std::optional, never a Pass-defaulted placeholder, mirroring
// CalibrationSession's own optionality exactly.
std::string renderCalibration(const ReportPayload& p) {
    const auto& c = p.calibration;
    std::string body;
    if (!c.performed) {
        body += "<p>calibration check not performed.</p>";
        return section("calibration", "Calibration", body);
    }
    body += kv("Pre-check level", escapeHtml(formatTrim(c.start.measuredLevelDb)));
    body += kv("Pre-check time (ms, Unix epoch)", std::to_string(c.start.unixMs));
    body += kv("Post-check level", escapeHtml(formatTrim(c.end.measuredLevelDb)));
    body += kv("Post-check time (ms, Unix epoch)", std::to_string(c.end.unixMs));
    body += kv("Drift", escapeHtml(formatTrim(c.driftDb)));
    body += kv("Calibrator nominal level",
              escapeHtml(formatTrim(c.start.level.nominalDb)) +
                  (c.start.level.operatorSupplied ? " (operator-supplied)" : ""));
    body += kv("Compared against", escapeHtml(std::string(c.clause)));
    if (c.verdict) {
        body += kv("Verdict", *c.verdict == rta::measure::CalibrationVerdict::Pass ? "Pass" : "Fail");
    }
    return section("calibration", "Calibration", body);
}

// One polyline per series, a fixed 1000x120 viewBox so a very long session
// scales without a decimation policy the report would have to invent.
//
// Fix round (PR #28 verifier, MEDIUM): markers used to map blockIndex via
// `% 1000`, a DIFFERENT scale from the trace's own `(idx-first)/span*width`
// -- on a 28800-block session an alarm at the last block landed at x=799
// instead of x=1000, nowhere near the point it annotates. Both now share
// ONE first/last range, computed once over every series, so a marker
// anywhere in the session lands on the same x-axis the trace itself uses.
//
// Fix round 3 (verifier MEDIUM): that first/last range used to come from
// `p.history` ALONE, but `appendHistoryAndMarkers` omits CalibrationInvalid
// blocks from history while still emitting Overload/Gap markers for EVERY
// block -- after a FAILED calibration brackets [0, latest] (the common
// case, restartSplLoggingForCalibration's own doc comment), history can be
// EMPTY while markers exist. An empty `first`/`last` used to send every
// marker to x=0 (all of them stacked on the left edge), and any marker past
// the last HISTORY point could land past the 1000-wide viewBox entirely.
// Folding `p.markers` into the same scan fixes both: the scale's own min/max
// now includes every point this SVG actually draws.
std::string renderHistory(const ReportPayload& p) {
    constexpr double kW = 1000.0, kH = 120.0, kFloor = -20.0, kCeil = 140.0;

    std::optional<std::uint64_t> first;
    std::optional<std::uint64_t> last;
    for (const auto& series : p.history) {
        for (const auto& pt : series.points) {
            if (!first || pt.blockIndex < *first) first = pt.blockIndex;
            if (!last || pt.blockIndex > *last) last = pt.blockIndex;
        }
    }
    for (const auto& marker : p.markers) {
        if (!first || marker.blockIndex < *first) first = marker.blockIndex;
        if (!last || marker.blockIndex > *last) last = marker.blockIndex;
    }
    const double span = (first && last && *last > *first) ? static_cast<double>(*last - *first) : 1.0;
    auto xFor = [&](std::uint64_t idx) -> double {
        if (!first) return 0.0;
        const double delta = idx >= *first ? static_cast<double>(idx - *first) : 0.0;
        return delta / span * kW;
    };

    std::string svg = "<svg class=\"strip\" viewBox=\"0 0 1000 120\" "
                      "xmlns=\"http://www.w3.org/2000/svg\">";

    // Fix round 3: make the dropped prefix/span VISIBLE, not just absent
    // from the trace -- a reader seeing a shorter-than-expected trace with
    // no explanation cannot tell "nothing was logged" from "something was
    // logged and excluded" (record sec.15 A2's own reasoning, applied to the
    // strip rather than just the block counts). Drawn FIRST so the trace and
    // markers sit on top of it.
    if (p.validity.excludedBlockRange) {
        const double x1 = xFor(p.validity.excludedBlockRange->startBlockIndex);
        const double x2 = xFor(p.validity.excludedBlockRange->endBlockIndex);
        svg += "<rect class=\"excluded-region\" x=\"" + std::to_string(std::min(x1, x2)) +
              "\" y=\"0\" width=\"" + std::to_string(std::max(x1, x2) - std::min(x1, x2)) +
              "\" height=\"120\" />";
    }

    for (const auto& series : p.history) {
        if (series.points.empty()) continue;
        std::string points;
        for (const auto& pt : series.points) {
            const double x = xFor(pt.blockIndex);
            const double clamped = std::min(std::max(pt.valueDb, kFloor), kCeil);
            const double y = kH - (clamped - kFloor) / (kCeil - kFloor) * kH;
            points += std::to_string(x) + "," + std::to_string(y) + " ";
        }
        svg += "<polyline class=\"trace\" points=\"" + points + "\" />";
    }
    for (const auto& marker : p.markers) {
        const std::string cls = marker.kind == rta::measure::SplMarkerKind::Alarm      ? "marker-alarm"
                                : marker.kind == rta::measure::SplMarkerKind::Overload ? "marker-overload"
                                : marker.kind == rta::measure::SplMarkerKind::Gap      ? "marker-gap"
                                                                                       : "";
        if (cls.empty()) continue;
        const std::string x = std::to_string(xFor(marker.blockIndex));
        svg += "<line class=\"" + cls + "\" x1=\"" + x + "\" x2=\"" + x + "\" y1=\"0\" y2=\"120\" />";
    }
    svg += "</svg>";
    return section("history", "Time history", svg);
}

std::string renderValidity(const ReportPayload& p) {
    const auto& v = p.validity;
    std::string body;
    body += kv("Total blocks", std::to_string(v.totalBlocks));
    body += kv("Excluded from compliance windows", std::to_string(v.excludedBlocks));
    body += kv("Overload blocks", std::to_string(v.overloadBlocks));
    body += kv("Under-range blocks", std::to_string(v.underRangeBlocks));
    body += kv("Dropped blocks", std::to_string(v.droppedBlocks));
    body += kv("Gap blocks", std::to_string(v.gapBlocks));
    body += kv("Total samples lost to gaps", std::to_string(v.droppedSamplesTotal));
    body += kv("Refused metrics (configured beyond the cap)", std::to_string(v.refusedMetrics));
    body += kv("Bytes discarded (truncated final line)", std::to_string(v.bytesDiscarded));
    body += kv("Ln / dose / alarm source",
              v.lnDoseAlarmFromLiveSession
                  ? "live session, read at export time"
                  : "unavailable -- session was not live when this report was built");
    // Fix round 3 (verifier MEDIUM): named here in text, not just shaded on
    // the strip (renderHistory), so a reader who only reads this table --
    // never the SVG -- still learns the prefix was dropped, and exactly
    // which blocks.
    body += kv("Excluded block range (calibration invalid)",
              v.excludedBlockRange
                  ? std::to_string(v.excludedBlockRange->startBlockIndex) + " .. " +
                        std::to_string(v.excludedBlockRange->endBlockIndex)
                  : "none");
    body += "<table><tr><th>Segment</th></tr>";
    for (const auto& seg : v.segmentPaths) body += "<tr><td>" + escapeHtml(seg) + "</td></tr>";
    body += "</table>";
    body += "<p class=\"honesty\">This report's content list is assembled from market "
            "practice; it is not claimed conformant with ISO 1996-2:2017 clause 13, "
            "whose body is paywalled and unread by this project.</p>";
    // Task W2-E2b fix round (MEDIUM finding): overload/gap markers ARE
    // derived from the log's own block flags (this section's own counts
    // above), but an alarm FIRED/CLEARED transition is state SplAlarms holds
    // only in memory -- nothing in the log format records it, so a session
    // exported after the live view is gone cannot recover it. Stated rather
    // than silently absent.
    body += "<p class=\"honesty\">Alarm transition markers are not recorded in this log "
            "format.</p>";
    return section("validity", "Validity", body);
}

}  // namespace rta::splexport::detail
