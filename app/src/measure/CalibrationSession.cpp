// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
#include "measure/CalibrationSession.h"

#include "measure/Levels.h"
#include "measure/SplMeter.h"

#include "rta/meter/Block.h"

#include <cmath>

namespace rta::measure {

namespace {

/// Measures `samples` through the Z-weighted chain a real session uses --
/// `SplMeter` + `BlockAccumulator`, never `rta::meter::Leq` (SPL-R3) -- with
/// NO offset applied, so the result is the raw mean-square level
/// `SplMeter::blockLevelDb` would publish before calibration corrects it.
///
/// `blockSeconds` is derived from the span's own length rather than fixed at
/// the usual 1.0 s, so this closes exactly ONE block over whatever capture a
/// caller hands in -- not just a round number of seconds -- with nothing left
/// over for `poll()` to never return.
double measureRawZLevelDb(std::span<const float> samples, double sampleRate) noexcept {
    if (samples.empty() || !(sampleRate > 0.0)) {
        return kLevelFloorDb;
    }
    SplConfig scratch;
    scratch.blockSeconds = static_cast<double>(samples.size()) / sampleRate;
    scratch.referenceOffsetDb = 0.0;
    SplMeter meter(scratch, rta::dsp::WeightingType::Z, sampleRate);
    meter.push(samples);
    const auto block = meter.poll();
    if (!block.has_value()) {
        return kLevelFloorDb;
    }
    return SplMeter::blockLevelDb(*block, 0.0);
}

CalibrationCheck buildCheck(CalibrationLevel level, std::span<const float> samples,
                            double sampleRate, std::uint64_t unixMs) noexcept {
    CalibrationCheck check;
    check.level = level;
    check.measuredLevelDb = measureRawZLevelDb(samples, sampleRate);
    check.offsetDb = rta::meter::calibrationOffsetDb(level.nominalDb, check.measuredLevelDb);
    check.unixMs = unixMs;
    return check;
}

}  // namespace

void CalibrationSession::recordStartCheck(CalibrationLevel level, std::span<const float> samples,
                                          double sampleRate, std::uint64_t unixMs) noexcept {
    if (samples.empty() || !(sampleRate > 0.0)) return;
    startCheck_ = buildCheck(level, samples, sampleRate, unixMs);
}

void CalibrationSession::recordEndCheck(CalibrationLevel level, std::span<const float> samples,
                                        double sampleRate, std::uint64_t unixMs) noexcept {
    if (samples.empty() || !(sampleRate > 0.0)) return;
    endCheck_ = buildCheck(level, samples, sampleRate, unixMs);
}

std::optional<double> CalibrationSession::driftDb() const noexcept {
    if (!startCheck_.has_value() || !endCheck_.has_value()) {
        return std::nullopt;
    }
    // ABS, deliberately: a mic that went QUIETER between the two checks
    // (offsetEnd - offsetStart < 0) is exactly as invalid as one that went
    // louder. Dropping this abs() is the mutation this task's own acceptance
    // (A3) is built to catch -- see test_calibration.cpp's fixture, which
    // gives the two checks opposite-signed raw differences on purpose.
    return std::abs(endCheck_->offsetDb - startCheck_->offsetDb);
}

std::optional<CalibrationVerdict> CalibrationSession::verdict() const noexcept {
    const auto drift = driftDb();
    if (!drift.has_value()) {
        return std::nullopt;
    }
    return *drift <= kMaxDriftDb ? CalibrationVerdict::Pass : CalibrationVerdict::Fail;
}

CalibrationReportFields CalibrationSession::reportFields() const noexcept {
    CalibrationReportFields fields;
    fields.clause = kClause;
    if (!startCheck_.has_value() || !endCheck_.has_value()) {
        return fields;  // `performed` stays false; every other field its default.
    }
    fields.performed = true;
    fields.start = *startCheck_;
    fields.end = *endCheck_;
    fields.driftDb = *driftDb();
    fields.verdict = *verdict();
    return fields;
}

}  // namespace rta::measure
