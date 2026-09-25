// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Task W2-E2b fix round (verifier MEDIUM finding, mutant M2): the
// composition-root step "a calibration START check completed -> build the
// SplConfig the fresh log should carry" was inline in
// MainComponentCalibration.cpp and untested -- a dropped
// `config.referenceOffsetDb = ...` line compiled clean and would have logged
// an uncalibrated session forever. Lifted out, header-only, so it is provable
// OFF -- the same reasoning `SplLoggingDecision.h` already applies to the
// off/on/epoch-changed decision (PR #31).
#pragma once

#include "measure/CalibrationSession.h"
#include "measure/SplConfig.h"

namespace rta::measure {

/// What `AnalysisThread::enableSplLogging` needs once a calibration START
/// check has set `session.referenceOffsetDb()`: the `SplConfig` a fresh log
/// should carry, and the calibrator's own nominal level for the log header
/// (`SplLogHeaderInfo::calibratorLevelDb`).
struct CalibratedSplLogConfig {
    SplConfig config;
    double calibratorLevelDb = 0.0;
};

/// Precondition: `session.hasStartCheck()` (the same precondition
/// `CalibrationSession::referenceOffsetDb()`/`startCheck()` already state).
/// `config` is otherwise `SplConfig{}` defaults -- SPL-R11, no preferences
/// store -- with only the two calibration facts set, matching every other
/// caller of `startFreshSplLogWithConfig`.
[[nodiscard]] inline CalibratedSplLogConfig calibratedSplLogConfig(
    const CalibrationSession& session) noexcept {
    CalibratedSplLogConfig result;
    result.config.referenceOffsetDb = session.referenceOffsetDb();
    result.config.calibrated = true;
    result.calibratorLevelDb = session.startCheck().level.nominalDb;
    return result;
}

}  // namespace rta::measure
