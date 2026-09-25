// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
// Lane L6a task W4a-A (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md).
//
// INTERNAL to the SplReport family -- not part of the public API in
// SplReport.h. Split out of SplReport.cpp (PR #28 fix round) so the nine
// record sec.9 section renderers, and the small text helpers they share,
// have a home that is not SplReport.cpp itself, keeping that file under the
// 400-line hard cap. `rta::splexport::detail` is a second, internal
// vocabulary -- nothing here is called from outside this export family.
#pragma once

#include "export/SplReport.h"

#include "rta/dsp/Weighting.h"
#include "rta/meter/Detector.h"

#include <optional>
#include <string>
#include <string_view>

namespace rta::splexport::detail {

[[nodiscard]] std::string escapeHtml(std::string_view text);
[[nodiscard]] std::string section(std::string_view id, std::string_view title, std::string body);
[[nodiscard]] std::string kv(std::string_view label, std::string value);
[[nodiscard]] std::string absentSpan(std::string_view reason);
[[nodiscard]] std::string dbOrAbsent(std::optional<double> value);
[[nodiscard]] std::string detectorLetter(rta::meter::TimeWeighting d);
[[nodiscard]] std::string percentText(double percent);
[[nodiscard]] std::string intervalText(double seconds);
[[nodiscard]] std::string lnLabel(rta::dsp::WeightingType w, rta::meter::TimeWeighting d, double percent,
                                  double windowSeconds);
// One decimal, no unit suffix -- for a bare number that is neither a dB
// level, a hertz value nor a 0..1 agreement (record sec.9's dose exchange
// rate `q`), so it is never squeezed into a formatter that would print a
// unit the quantity does not have.
[[nodiscard]] std::string oneDecimal(double value);
// Dose is a percentage that can exceed 100, NOT the 0..1 ratio
// `formatAgreement` means.
[[nodiscard]] std::string percentDisplay(double percent);
[[nodiscard]] std::string percentOrAbsent(std::optional<double> value);

[[nodiscard]] std::string renderIdentification(const ReportPayload& p);
[[nodiscard]] std::string renderInstrument(const ReportPayload& p);
[[nodiscard]] std::string renderCalibration(const ReportPayload& p);
[[nodiscard]] std::string renderSettings(const ReportPayload& p);
[[nodiscard]] std::string renderMetrics(const ReportPayload& p);
[[nodiscard]] std::string renderDose(const ReportPayload& p);
[[nodiscard]] std::string renderHistory(const ReportPayload& p);
[[nodiscard]] std::string renderValidity(const ReportPayload& p);
[[nodiscard]] std::string renderIntegrity(const ReportPayload& p);

}  // namespace rta::splexport::detail
