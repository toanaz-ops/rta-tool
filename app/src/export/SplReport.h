// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
// Lane L6a task W4a-A (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md sec.9, sec.11) plus W3-C
// (calibration in the report).
//
// PURE CONTENT: renderReport/renderViewerShell take a value and return a
// string. Nothing here opens a file or a socket -- the EqTextExport.h /
// SplLog.h precedent. The renderer performs NO aggregation: every field
// below is a value the caller already computed (Wave 2/3's job), because a
// report that re-derives a number from raw blocks is a report that can
// disagree with the log that is its own evidence.
#pragma once

#include "measure/CalibrationSession.h"
#include "measure/SplConfig.h"
#include "measure/SplHistory.h"

#include "rta/dsp/Weighting.h"
#include "rta/meter/Detector.h"
#include "rta/meter/Dose.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rta::splexport {

/// Record sec.9 item 5, one row per configured metric. `intervalSeconds` is
/// the reporting period THIS Leq covers (record sec.9's "Leq over the
/// reporting period" -- distinct from any live windowed value on screen).
/// Every field but `id`/`weighting`/`detector`/`intervalSeconds` is absent
/// rather than a placeholder zero: a session with no dose accumulated, or a
/// rank outside the histogram's span, prints "absent" and says why, never 0
/// (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
struct ReportMetricResult {
    std::string id;
    rta::dsp::WeightingType weighting = rta::dsp::WeightingType::A;
    rta::meter::TimeWeighting detector = rta::meter::TimeWeighting::Fast;
    double intervalSeconds = 0.0;
    std::optional<double> leqDb;
    std::optional<double> lmaxDb;
    std::optional<double> lminDb;
    std::optional<double> lpeakDb;  ///< L_Cpeak, SAMPLED (record sec.12) -- not true peak
    std::optional<double> selDb;
    /// Six slots, matching `SplConfig::lnPercents` -- the percentage each
    /// slot names travels WITH the value so the label can be built without a
    /// second lookup into config.
    std::array<std::optional<double>, 6> lnDb{};
    std::array<double, 6> lnPercents{};
};

/// Record sec.9 item 6, one accumulator's row. `label` is DATA, never a
/// regulator's name typed into `core/` (record sec.11) -- the app assigns it
/// ("NIOSH REL", "OSHA PEL", or whatever the operator called their own
/// preset).
struct ReportDoseResult {
    std::string label;
    rta::meter::DoseSettings settings;
    double percent = 0.0;
    double projectedPercent = 0.0;
    double elapsedSeconds = 0.0;
    double twaDb = 0.0;
    double exposureLevel8hDb = 0.0;  ///< L_EX,8h, record sec.7's exposureLevelDb()
};

/// Record sec.9 item 8 plus record sec.15 A2's five-way count -- every block
/// that did NOT reach a compliance window is accounted for by name, not
/// folded into a single "excluded" figure that cannot say why.
struct ReportValidity {
    std::uint64_t totalBlocks = 0;
    std::uint64_t excludedBlocks = 0;    ///< CalibrationInvalid only (record sec.15 A2)
    std::uint64_t overloadBlocks = 0;
    std::uint64_t underRangeBlocks = 0;
    std::uint64_t droppedBlocks = 0;
    std::uint64_t gapBlocks = 0;
    std::uint64_t droppedSamplesTotal = 0;
    /// Configured metrics the session could not serve (`SplConfig::
    /// kMaxMetrics`, record sec.15 A5) -- 0 normally.
    std::uint32_t refusedMetrics = 0;
    std::vector<std::string> segmentPaths;
};

/// One point of the time-history strip (record sec.9 item 7): one metric's
/// windowed reading at one block index, already resolved to dB.
struct ReportHistoryPoint {
    std::uint64_t blockIndex = 0;
    double valueDb = 0.0;
};

struct ReportHistorySeries {
    std::string metricId;
    std::vector<ReportHistoryPoint> points;
};

/// The frozen payload (record sec.9). Nothing here is a wall clock read at
/// render time and nothing is aggregated by the renderer -- see the file
/// header. `logSegments` are the raw bytes item 9's hash is computed over;
/// the payload does not carry a pre-computed hash, because a payload cannot
/// honestly know its own hash before the renderer that hashes it runs.
struct ReportPayload {
    // 1. Identification (record sec.9 item 1)
    std::string venue, event, operatorName, company, engineer, productionCompany, notes;
    // 2. Instrument (item 2)
    std::string appName, appVersion, buildId, device, channel;
    double sampleRate = 0.0;
    // 3. Calibration (item 3, task W3-C). `performed == false` prints
    // "calibration check not performed" -- Wave 3's own cut fallback.
    rta::measure::CalibrationReportFields calibration;
    // 4. Settings (item 4)
    rta::measure::SplConfig config;
    // 5. Results per configured metric (item 5)
    std::vector<ReportMetricResult> metrics;
    // 6. Dose (item 6)
    std::array<ReportDoseResult, 2> dose;
    // 7. Time history (item 7)
    std::vector<ReportHistorySeries> history;
    std::vector<rta::measure::SplMarker> markers;
    // 8. Validity (item 8)
    ReportValidity validity;
    // 9. Integrity (item 9) -- the bytes renderReport hashes.
    std::vector<std::string> logSegments;
};

/// SHA-256 over the concatenation of `payload.logSegments`, hex-encoded
/// lower-case, 64 characters. Exposed so a test can pin it against FIPS
/// 180-4's own vectors independently of any report payload.
[[nodiscard]] std::string sha256Hex(std::string_view data);

/// sec.9: one self-contained HTML document, inline CSS, inline SVG, no
/// external asset -- the frozen report. Defines `window.__SPL_PAYLOAD__`
/// exactly once (A1b); every dB/Hz/agreement number in it is
/// `rta::view::formatTrim`/`formatHz`/`formatAgreement`'s own output (A7).
[[nodiscard]] std::string renderReport(const ReportPayload& payload);

/// The same document with no payload embedded (A1b: `window.__SPL_PAYLOAD__`
/// appears zero times) -- Wave 4b's shell, built now per the orchestrator's
/// 2026-09-25 decision so A1b is provable even though 4b itself is cut. It
/// carries the sec.12-constraint-2-untested-for-the-viewer sentence, because
/// nothing will ever fetch a payload into it in this lane.
[[nodiscard]] std::string renderViewerShell();

}  // namespace rta::splexport
