// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
// Lane L6a task W4a-A / W3-C (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md sec.9, sec.11).
//
// The nine record sec.9 section renderers, split out of SplReport.cpp (PR
// #28 fix round) to keep that file under the 400-line hard cap. Pure
// content -- see SplReport.cpp's own header comment for the file family's
// shape.
#include "export/SplReportSections.h"

#include "export/SplReportScript.h"
#include "export/SplReportStyle.h"
#include "view/Readouts.h"

#include <algorithm>
#include <format>
#include <sstream>

namespace rta::splexport::detail {

using rta::view::formatHz;
using rta::view::formatTrim;

std::string escapeHtml(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string section(std::string_view id, std::string_view title, std::string body) {
    return "<section class=\"section\" id=\"" + std::string(id) + "\"><h2>" +
           escapeHtml(title) + "</h2>" + body + "</section>";
}

std::string kv(std::string_view label, std::string value) {
    return "<div class=\"kv\"><span class=\"label\">" + escapeHtml(label) +
           "</span><span class=\"value\">" + value + "</span></div>";
}

std::string absentSpan(std::string_view reason) {
    return "<span class=\"absent\">absent (" + escapeHtml(reason) + ")</span>";
}

std::string dbOrAbsent(std::optional<double> value) {
    return value ? escapeHtml(formatTrim(*value)) : absentSpan("no data");
}

std::string detectorLetter(rta::meter::TimeWeighting d) {
    switch (d) {
        case rta::meter::TimeWeighting::Fast: return "F";
        case rta::meter::TimeWeighting::Slow: return "S";
        case rta::meter::TimeWeighting::Impulse: return "I";
    }
    return "F";
}

std::string percentText(double percent) {
    std::ostringstream ss;
    if (percent == static_cast<double>(static_cast<long long>(percent))) ss << static_cast<long long>(percent);
    else ss << percent;
    return ss.str();
}

std::string intervalText(double seconds) {
    const auto whole = static_cast<long long>(seconds + 0.5);
    if (whole > 0 && whole % 3600 == 0) return std::to_string(whole / 3600) + "h";
    if (whole > 0 && whole % 60 == 0) return std::to_string(whole / 60) + "min";
    return std::to_string(whole) + "s";
}

std::string lnLabel(rta::dsp::WeightingType w, rta::meter::TimeWeighting d, double percent,
                    double windowSeconds) {
    return "L_" + std::string(rta::dsp::toString(w)) + detectorLetter(d) + percentText(percent) +
           "," + intervalText(windowSeconds);
}

std::string oneDecimal(double value) {
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(1);
    ss << value;
    return ss.str();
}

std::string percentDisplay(double percent) { return oneDecimal(percent) + " %"; }

std::string exchangeRateDisplay(double q) { return std::format("{:.7f}", q); }

std::string percentOrAbsent(std::optional<double> value) {
    return value ? escapeHtml(percentDisplay(*value)) : absentSpan("no data");
}

std::string stateLabel(rta::measure::SplAlarmState state) {
    switch (state) {
        case rta::measure::SplAlarmState::Filling: return "Filling";
        case rta::measure::SplAlarmState::Clear: return "Clear";
        case rta::measure::SplAlarmState::Fired: return "Fired";
    }
    return "Filling";
}

std::string stateClass(rta::measure::SplAlarmState state) {
    switch (state) {
        case rta::measure::SplAlarmState::Filling: return "state-filling";
        case rta::measure::SplAlarmState::Clear: return "state-clear";
        case rta::measure::SplAlarmState::Fired: return "state-fired";
    }
    return "state-filling";
}

std::string renderIdentification(const ReportPayload& p) {
    std::string body;
    body += kv("Venue", escapeHtml(p.venue));
    body += kv("Event", escapeHtml(p.event));
    body += kv("Operator", escapeHtml(p.operatorName));
    body += kv("Company", escapeHtml(p.company));
    body += kv("Engineer", escapeHtml(p.engineer));
    body += kv("Production company", escapeHtml(p.productionCompany));
    body += kv("Notes", escapeHtml(p.notes));
    return section("identification", "Identification", body);
}

std::string renderInstrument(const ReportPayload& p) {
    std::string body;
    body += kv("Application", escapeHtml(p.appName) + " " + escapeHtml(p.appVersion));
    body += kv("Build", escapeHtml(p.buildId));
    body += kv("Device", escapeHtml(p.device));
    body += kv("Channel", escapeHtml(p.channel));
    body += kv("Sample rate", escapeHtml(formatHz(p.sampleRate)));
    body += "<p class=\"honesty\">Analytic frequency weighting is verified within 0.05 dB of "
            "IEC 61672-1:2013 Table 3 (not Table 2); the digital filter's approximation error "
            "is published beside it. This report makes no claim of conformance to any "
            "IEC 61672-1 accuracy class. The instrument's linear operating range, overload and "
            "under-range behaviour have not been verified against IEC 61672-1 clause 3.28's "
            "validity definition.</p>";
    // Fix round (PR #28 verifier, HIGH): this sentence used to live only in
    // renderViewerShell(), which the product never emits -- the owner
    // decision (2026-09-25) says it goes in the REPORT. Wave 4b (the served
    // viewer) was cut before shipping, so record sec.12 constraint 2's
    // rounding obligation is recorded as untested for the viewer.
    body += "<p class=\"honesty\">Wave 4b (the served viewer) was cut before shipping "
            "(owner decision, 2026-09-25); record sec.12 constraint 2's rounding "
            "obligation is therefore recorded as untested for the viewer.</p>";
    return section("instrument", "Instrument", body);
}

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

std::string renderSettings(const ReportPayload& p) {
    const auto& cfg = p.config;
    std::string body;
    body += kv("Block interval", escapeHtml(formatTrim(cfg.blockSeconds)));
    body += kv("Reference offset", escapeHtml(formatTrim(cfg.referenceOffsetDb)));
    body += kv("Calibrated", cfg.calibrated ? "yes" : "no");
    body += kv("Log span", escapeHtml(intervalText(cfg.logSpanSeconds)));
    body += kv("Segment size (blocks)", std::to_string(cfg.segmentBlocks));

    body += "<table><tr><th>Metric</th><th>Weighting</th><th>Detector</th>"
            "<th>Window</th></tr>";
    for (const auto& m : cfg.metrics) {
        body += "<tr><td>" + escapeHtml(m.id) + "</td><td>" +
                std::string(rta::dsp::toString(m.weighting)) + "</td><td>" +
                std::string(rta::meter::toString(m.detector)) + "</td><td>" +
                escapeHtml(intervalText(static_cast<double>(m.windowBlocks) * cfg.blockSeconds)) +
                "</td></tr>";
    }
    body += "</table>";

    body += "<table><tr><th>Alarm on</th><th>Limit</th><th>Window</th></tr>";
    for (const auto& a : cfg.alarms) {
        body += "<tr><td>" + escapeHtml(a.metricId) + "</td><td>" + escapeHtml(formatTrim(a.limitDb)) +
                "</td><td>" +
                escapeHtml(intervalText(static_cast<double>(a.windowBlocks) * cfg.blockSeconds)) +
                "</td></tr>";
    }
    body += "</table>";

    // Fix round (PR #28 verifier, MEDIUM): the alarms' LIVE verdict --
    // `state` is SERVER-computed (record sec.9) and rendered with the
    // existing `.state-*` CSS classes, never re-derived here from
    // `limitDb`. Filling is not Clear: record sec.15 A6's own words --
    // Clear would claim "compared, and under the limit" for a comparison
    // that never ran.
    if (!p.alarms.empty()) {
        body += "<table><tr><th>Alarm status</th><th>State</th><th>Headroom</th></tr>";
        for (const auto& a : p.alarms) {
            const bool filling = a.state == rta::measure::SplAlarmState::Filling;
            body += "<tr><td>" + escapeHtml(a.metricId) + "</td><td class=\"" + stateClass(a.state) +
                    "\">" + stateLabel(a.state) +
                    (filling ? " -- window not yet full, not compared" : "") + "</td><td>" +
                    dbOrAbsent(a.headroomDb) + "</td></tr>";
        }
        body += "</table>";
    }

    // Fix round (PR #28 verifier, MEDIUM): record sec.9 item 4, "dose
    // preset with L_c, T_c, q and threshold" -- the four settings a dose
    // accumulator was configured with, not its RESULT (that is the Dose
    // section). `q` is a dimensionless exchange-rate denominator, not a
    // sound level, so it never goes through `formatTrim` (which would
    // print a false "dB" unit on it) -- and round-3's own fix, it prints to
    // SEVEN decimals via `exchangeRateDisplay`, matching the record's own
    // table, because one decimal made NIOSH's computed 9.9657843 read as
    // "10.0", textually the exact q=10 value it exists to be distinct from.
    body += "<table><tr><th>Dose preset</th><th>L_c</th><th>q</th><th>Threshold</th>"
            "<th>T_c</th></tr>";
    for (std::size_t i = 0; i < cfg.dose.size(); ++i) {
        const auto& d = cfg.dose[i];
        body += "<tr><td>" + std::to_string(i + 1) + "</td><td>" + escapeHtml(formatTrim(d.criterionLevelDb)) +
                "</td><td>" + escapeHtml(exchangeRateDisplay(d.q)) + "</td><td>" +
                escapeHtml(formatTrim(d.thresholdDb)) + "</td><td>" +
                escapeHtml(intervalText(d.criterionSeconds)) + "</td></tr>";
    }
    body += "</table>";
    return section("settings", "Settings", body);
}

std::string renderMetrics(const ReportPayload& p) {
    std::string body;
    for (const auto& m : p.metrics) {
        body += "<h3>" + escapeHtml(m.id) + " (" + std::string(rta::dsp::toString(m.weighting)) +
                std::string(rta::meter::toString(m.detector)) + ")</h3><table>";
        body += "<tr><td>Leq (" + escapeHtml(intervalText(m.intervalSeconds)) + ")</td><td>" +
                dbOrAbsent(m.leqDb) + "</td></tr>";
        body += "<tr><td>Lmax</td><td>" + dbOrAbsent(m.lmaxDb) + "</td></tr>";
        body += "<tr><td>Lmin</td><td>" + dbOrAbsent(m.lminDb) + "</td></tr>";
        body += "<tr><td>Lpeak (C, sampled)</td><td>" + dbOrAbsent(m.lpeakDb) + "</td></tr>";
        body += "<tr><td>SEL</td><td>" + dbOrAbsent(m.selDb) + "</td></tr>";
        for (std::size_t i = 0; i < m.lnDb.size(); ++i) {
            const std::string label = lnLabel(m.weighting, m.detector, m.lnPercents[i], m.intervalSeconds);
            body += "<tr><td>" + escapeHtml(label) + "</td><td>" + dbOrAbsent(m.lnDb[i]) + "</td></tr>";
        }
        body += "</table>";
    }
    return section("results", "Results per configured metric", body);
}

std::string renderDose(const ReportPayload& p) {
    std::string body = "<table><tr><th>Preset</th><th>D%</th><th>Projected D%</th>"
                       "<th>Elapsed</th><th>Criterion</th><th>TWA</th><th>L_EX,8h</th></tr>";
    for (const auto& d : p.dose) {
        if (d.label.empty()) continue;
        body += "<tr><td>" + escapeHtml(d.label) + "</td><td>" + percentOrAbsent(d.percent) +
                "</td><td>" + percentOrAbsent(d.projectedPercent) + "</td><td>" +
                escapeHtml(intervalText(d.elapsedSeconds)) + "</td><td>" +
                escapeHtml(intervalText(d.settings.criterionSeconds)) + "</td><td>" +
                dbOrAbsent(d.twaDb) + "</td><td>" + dbOrAbsent(d.exposureLevel8hDb) +
                "</td></tr>";
    }
    body += "</table>";
    return section("dose", "Dose", body);
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
    const double span = (first && last && *last > *first) ? static_cast<double>(*last - *first) : 1.0;
    auto xFor = [&](std::uint64_t idx) -> double {
        if (!first) return 0.0;
        const double delta = idx >= *first ? static_cast<double>(idx - *first) : 0.0;
        return delta / span * kW;
    };

    std::string svg = "<svg class=\"strip\" viewBox=\"0 0 1000 120\" "
                      "xmlns=\"http://www.w3.org/2000/svg\">";
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
        const std::string cls = marker.kind == rta::measure::SplMarkerKind::Alarm ? "marker-alarm"
                                : marker.kind == rta::measure::SplMarkerKind::Overload ? "marker-overload"
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
    body += "<table><tr><th>Segment</th></tr>";
    for (const auto& seg : v.segmentPaths) body += "<tr><td>" + escapeHtml(seg) + "</td></tr>";
    body += "</table>";
    body += "<p class=\"honesty\">This report's content list is assembled from market "
            "practice; it is not claimed conformant with ISO 1996-2:2017 clause 13, "
            "whose body is paywalled and unread by this project.</p>";
    return section("validity", "Validity", body);
}

std::string renderIntegrity(const ReportPayload& p) {
    std::string joined;
    for (const auto& seg : p.logSegments) joined += seg;
    const std::string hash = sha256Hex(joined);
    std::string body = kv("SHA-256 over the log segments", "<span class=\"hash\">" + hash + "</span>");
    body += "<p class=\"honesty\">A tamper-evidence hash, not a signature: a signature "
            "needs a key and a story about where the key lives, which this project does "
            "not have.</p>";
    return section("integrity", "Integrity", body);
}

}  // namespace rta::splexport::detail
