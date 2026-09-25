// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
// Lane L6a task W4a-A (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md sec.9, sec.11).
#include "export/SplReport.h"
#include "export/SplReportScript.h"
#include "export/SplReportStyle.h"

#include "view/Readouts.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>
#include <vector>

namespace rta::splexport {

namespace {

using rta::view::formatHz;
using rta::view::formatTrim;

// SHA-256 (FIPS 180-4), from the public spec -- no third-party dependency
// (plan A5). test_spl_report.cpp pins it against FIPS 180-4's own vectors.
constexpr std::array<std::uint32_t, 64> kK = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

[[nodiscard]] constexpr std::uint32_t rotr(std::uint32_t x, int n) noexcept {
    return (x >> n) | (x << (32 - n));
}

[[nodiscard]] std::array<std::uint32_t, 8> sha256Digest(std::string_view data) {
    std::array<std::uint32_t, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::vector<std::uint8_t> msg(data.begin(), data.end());
    const std::uint64_t bitLen = static_cast<std::uint64_t>(msg.size()) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<std::uint8_t>(bitLen >> (i * 8)));

    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(msg[chunk + i * 4]) << 24) |
                   (static_cast<std::uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(msg[chunk + i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(msg[chunk + i * 4 + 3]);
        }
        for (std::size_t s = 16; s < 64; ++s) {
            const std::uint32_t s0 = rotr(w[s - 15], 7) ^ rotr(w[s - 15], 18) ^ (w[s - 15] >> 3);
            const std::uint32_t s1 = rotr(w[s - 2], 17) ^ rotr(w[s - 2], 19) ^ (w[s - 2] >> 10);
            w[s] = w[s - 16] + s0 + w[s - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (std::size_t s = 0; s < 64; ++s) {
            const std::uint32_t bigS1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = hh + bigS1 + ch + kK[s] + w[s];
            const std::uint32_t bigS0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = bigS0 + maj;
            hh = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    return h;
}

[[nodiscard]] std::string escapeHtml(std::string_view text) {
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

[[nodiscard]] std::string section(std::string_view id, std::string_view title, std::string body) {
    return "<section class=\"section\" id=\"" + std::string(id) + "\"><h2>" +
           escapeHtml(title) + "</h2>" + body + "</section>";
}

[[nodiscard]] std::string kv(std::string_view label, std::string value) {
    return "<div class=\"kv\"><span class=\"label\">" + escapeHtml(label) +
           "</span><span class=\"value\">" + value + "</span></div>";
}

[[nodiscard]] std::string absentSpan(std::string_view reason) {
    return "<span class=\"absent\">absent (" + escapeHtml(reason) + ")</span>";
}

[[nodiscard]] std::string dbOrAbsent(std::optional<double> value) {
    return value ? escapeHtml(formatTrim(*value)) : absentSpan("no data");
}

// ISO 1996-1 cl. 3.1.3's notation, e.g. "L_AF90,15min" -- never bare "L90".
[[nodiscard]] std::string detectorLetter(rta::meter::TimeWeighting d) {
    switch (d) {
        case rta::meter::TimeWeighting::Fast: return "F";
        case rta::meter::TimeWeighting::Slow: return "S";
        case rta::meter::TimeWeighting::Impulse: return "I";
    }
    return "F";
}

[[nodiscard]] std::string percentText(double percent) {
    std::ostringstream ss;
    if (percent == static_cast<double>(static_cast<long long>(percent))) ss << static_cast<long long>(percent);
    else ss << percent;
    return ss.str();
}

[[nodiscard]] std::string intervalText(double seconds) {
    const auto whole = static_cast<long long>(seconds + 0.5);
    if (whole > 0 && whole % 3600 == 0) return std::to_string(whole / 3600) + "h";
    if (whole > 0 && whole % 60 == 0) return std::to_string(whole / 60) + "min";
    return std::to_string(whole) + "s";
}

[[nodiscard]] std::string lnLabel(rta::dsp::WeightingType w, rta::meter::TimeWeighting d,
                                  double percent, double windowSeconds) {
    return "L_" + std::string(rta::dsp::toString(w)) + detectorLetter(d) + percentText(percent) +
           "," + intervalText(windowSeconds);
}

// Dose is a percentage that can exceed 100 -- NOT the 0..1 ratio
// formatAgreement means, so it gets its own one-decimal rendering rather
// than being squeezed into a formatter that names a different quantity.
[[nodiscard]] std::string percentDisplay(double percent) {
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(1);
    ss << percent << " %";
    return ss.str();
}

}  // namespace

std::string sha256Hex(std::string_view data) {
    const auto h = sha256Digest(data);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (const std::uint32_t word : h) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            out.push_back(kHex[(word >> shift) & 0xFu]);
        }
    }
    return out;
}

namespace {

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
    return section("instrument", "Instrument", body);
}

// W4a-A baseline: Wave 3 (calibration) is not wired in yet, so this section
// always prints the record sec.9 item 3 fallback sentence -- task W3-C
// (next commit) makes this conditional on `ReportPayload::calibration.
// performed` and fills in the pre/post pair.
std::string renderCalibration(const ReportPayload&) {
    return section("calibration", "Calibration", "<p>calibration check not performed.</p>");
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
        body += "<tr><td>" + escapeHtml(d.label) + "</td><td>" + escapeHtml(percentDisplay(d.percent)) +
                "</td><td>" + escapeHtml(percentDisplay(d.projectedPercent)) + "</td><td>" +
                escapeHtml(intervalText(d.elapsedSeconds)) + "</td><td>" +
                escapeHtml(intervalText(d.settings.criterionSeconds)) + "</td><td>" +
                escapeHtml(formatTrim(d.twaDb)) + "</td><td>" + escapeHtml(formatTrim(d.exposureLevel8hDb)) +
                "</td></tr>";
    }
    body += "</table>";
    return section("dose", "Dose", body);
}

/// One polyline per series, a fixed 1000x120 viewBox so a very long session
/// scales without a decimation policy the report would have to invent.
std::string renderHistory(const ReportPayload& p) {
    constexpr double kW = 1000.0, kH = 120.0, kFloor = -20.0, kCeil = 140.0;
    std::string svg = "<svg class=\"strip\" viewBox=\"0 0 1000 120\" "
                      "xmlns=\"http://www.w3.org/2000/svg\">";
    for (const auto& series : p.history) {
        if (series.points.empty()) continue;
        const auto first = series.points.front().blockIndex;
        const auto last = series.points.back().blockIndex;
        const double span = last > first ? static_cast<double>(last - first) : 1.0;
        std::string points;
        for (const auto& pt : series.points) {
            const double x = static_cast<double>(pt.blockIndex - first) / span * kW;
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
        svg += "<line class=\"" + cls + "\" x1=\"" + std::to_string(marker.blockIndex % 1000) +
               "\" x2=\"" + std::to_string(marker.blockIndex % 1000) + "\" y1=\"0\" y2=\"120\" />";
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

std::string documentShell(std::string_view title, std::string_view bodyExtra, std::string sections) {
    std::string out = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">";
    out += "<title>" + escapeHtml(title) + "</title>";
    out += "<style>" + std::string(kReportStyle) + "</style>";
    out += "</head><body>";
    out += "<h1>SPL measurement report</h1><p class=\"subtitle\">rtatool</p>";
    out += std::string(bodyExtra);
    out += sections;
    out += "<script>" + std::string(kReportScript) + "</script>";
    out += "</body></html>";
    return out;
}

}  // namespace

std::string renderReport(const ReportPayload& p) {
    std::string sections;
    sections += renderIdentification(p);
    sections += renderInstrument(p);
    sections += renderCalibration(p);
    sections += renderSettings(p);
    sections += renderMetrics(p);
    sections += renderDose(p);
    sections += renderHistory(p);
    sections += renderValidity(p);
    sections += renderIntegrity(p);

    std::string payloadJson = "<script>window.__SPL_PAYLOAD__ = {\"frozen\":true};</script>";
    return documentShell(p.event.empty() ? "SPL report" : p.event, payloadJson, sections);
}

std::string renderViewerShell() {
    std::string sections =
        "<p class=\"honesty\">This is the viewer shell with no payload embedded. "
        "Wave 4b (the served viewer) was cut before shipping (owner decision, "
        "2026-09-25); record sec.12 constraint 2's rounding obligation is "
        "therefore recorded as untested for the viewer.</p>";
    return documentShell("SPL viewer", "", sections);
}

}  // namespace rta::splexport
