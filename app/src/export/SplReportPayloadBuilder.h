// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
// Lane L6a task W2-E2b part B (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md
// "W2-E -- the wiring nobody was assigned"; plan Wave 4a-A; record
// docs/dsp/2026-09-16-spl-pro-l6a.md §9, §15 A2).
//
// PURE ORCHESTRATION, no aggregation of its own beyond what
// `rta::meter::combineBlocks` (core, already proven) already does: this file
// turns a session folder written by `SplLogWriter`/`writeCalibrationRecordFile`
// into the `ReportPayload` `renderReport` (SplReport.h, task W4a-A) already
// knows how to render. JUCE-free, so it is OFF-testable against real files
// on disk (`std::filesystem`/`std::ifstream`, the SplLogWriter.cpp precedent
// -- "framework-free" bars JUCE/Qt/an audio-device API, not file I/O).
#pragma once

#include "export/SplReport.h"
#include "measure/SplSnapshot.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rta::splexport {

/// What building a report from a session folder needs beyond the folder
/// itself. Identification text (record §9 item 1) has no other source than
/// the operator, and Ln/dose/alarm state (task part B's own instruction)
/// exists only while a session is running -- the composition root supplies
/// both, this builder never invents either.
struct SplReportBuildRequest {
    std::string sessionDir;
    /// Which channel's log(s) to read -- normally the measurement channel(s)
    /// this session actually logged. One `ReportMetricResult` row per
    /// channel that has a readable log.
    std::vector<int> channels;

    // Identification (record §9 item 1) -- operator-entered, no other source.
    std::string venue, event, operatorName, company, engineer, productionCompany, notes;
    // Instrument (item 2) -- the composition root's own facts. `sampleRate`
    // is NOT here: it is read back from the log header itself, the same
    // "the log is the evidence" reasoning record §10 already states.
    std::string appName, appVersion, buildId, device;

    /// Ln/dose/alarm state, read from the LIVE session at export time (task
    /// spec: "Ln/dose/alarm states are NOT in the log ... take them from the
    /// live SplBlockView at export time"). Absent when exporting after the
    /// session has already stopped -- `ReportValidity::lnDoseAlarmFromLiveSession`
    /// says so in the rendered report rather than a silent absence.
    std::optional<rta::measure::SplBlockView> liveView;
};

/// A per-channel absence (that channel wrote no readable segment) is
/// reported here rather than silently producing a shorter metrics list --
/// the "counted, never silent" shape the rest of this lane uses
/// (`SplBlockView::refusedMetrics`, `::logDroppedBlocks`, ...).
struct SplReportBuildResult {
    /// Absent only when NOT ONE requested channel had a readable log --
    /// nothing to build a report from at all.
    std::optional<ReportPayload> payload;
    std::vector<int> channelsWithNoLog;
};

/// Reads every `ch<N>.gen*.seg*.csv` segment under `request.sessionDir` for
/// each requested channel (oldest generation/segment first), the optional
/// `calibration.txt` written by `writeCalibrationRecordFile`
/// (SplCalibrationRecord.h, task part A), and folds them into a
/// `ReportPayload`:
///
/// - The whole-session Leq is `rta::meter::combineBlocks` over EVERY block
///   read, honouring `BlockFlag::CalibrationInvalid` membership (record §3,
///   §15 A2) -- never a running subtraction, the same recompute the live
///   publish path already performs for every other window.
/// - A calibration record whose verdict FAILED (drift > 0.5 dB) marks its
///   own bracketed block range `CalibrationInvalid` HERE, at read time --
///   the log on disk is never rewritten (W3-A A3, record §10 "append-only,
///   never rewritten"). A PASSING drift brackets nothing: the pair
///   certifies the blocks between it, it does not exclude them.
/// - No calibration record found, or `performed == false`: the report's
///   calibration section prints "calibration check not performed" (Wave 3's
///   own cut fallback, task plan W3-C) -- reached here for free, since
///   `CalibrationReportFields{}` already defaults that way.
[[nodiscard]] SplReportBuildResult buildReportPayload(const SplReportBuildRequest& request);

}  // namespace rta::splexport
