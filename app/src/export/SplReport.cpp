// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
// Lane L6a task W4a-A / W3-C (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md sec.9, sec.11).
//
// The document shell and the two public entry points. The SHA-256 hash
// (record sec.9 item 9) lives in SplReportHash.cpp and the nine section
// renderers live in SplReportSections.cpp/.h (PR #28 fix round split, so
// this file stays well clear of the 400-line hard cap while new content
// lands in the other two).
#include "export/SplReport.h"
#include "export/SplReportScript.h"
#include "export/SplReportSections.h"
#include "export/SplReportStyle.h"

namespace rta::splexport {

namespace {

using detail::escapeHtml;

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
    sections += detail::renderIdentification(p);
    sections += detail::renderInstrument(p);
    sections += detail::renderCalibration(p);
    sections += detail::renderSettings(p);
    sections += detail::renderMetrics(p);
    sections += detail::renderDose(p);
    sections += detail::renderHistory(p);
    sections += detail::renderValidity(p);
    sections += detail::renderIntegrity(p);

    // A SENTINEL, not the real ReportPayload serialised to JSON -- a
    // deliberate choice, not an oversight (PR #28 fix round, minor).
    // window.__SPL_PAYLOAD__'s only job in this lane is A1b's seam: the
    // report defines it exactly once, the viewer shell zero times, which is
    // how the two transports are told apart. Wave 4b (the served viewer) --
    // the only consumer that would ever READ this value off the wire -- was
    // cut before shipping (owner decision, 2026-09-25), so nothing in this
    // codebase ever parses it. Serialising the whole ReportPayload here
    // would duplicate L-API's own ApiSerialise machinery (app/src/api/
    // ApiSerialise*.cpp) for a value nothing reads, and it is exactly the
    // kind of parallel implementation a future editor would have to
    // remember to keep in sync with ApiSerialise by hand. If Wave 4b is
    // ever un-cut, the real serialisation belongs here, alongside the JS
    // that would need it.
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
