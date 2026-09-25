// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Lane L6a task W3-A (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md; record
// docs/dsp/2026-09-16-spl-pro-l6a.md §8, §13 Q2).
#pragma once

#include "measure/SplConfig.h"

#include "rta/dsp/Weighting.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace rta::measure {

/// IEC 60942's two nominal calibrator levels at 1 kHz. Ship both; record §8
/// argues the FLOW is the differentiator, not a copied factory-tolerance
/// number (A5), so any other value still works -- it is just recorded
/// differently (A4).
inline constexpr double kIec60942Level94Db = 94.0;
inline constexpr double kIec60942Level114Db = 114.0;

/// A calibrator's stated level, with whether it is one of IEC 60942's own two
/// or something the operator typed. `nominalDb` is never silently
/// normalised: it always holds exactly what `calibrationLevel` was given.
struct CalibrationLevel {
    double nominalDb = kIec60942Level94Db;
    bool operatorSupplied = false;
};

/// The only constructor. `operatorSupplied` is DERIVED from `nominalDb`
/// rather than a second argument a caller could pass inconsistently with the
/// number it names (A4).
[[nodiscard]] constexpr CalibrationLevel calibrationLevel(double nominalDb) noexcept {
    return CalibrationLevel{nominalDb,
                            nominalDb != kIec60942Level94Db && nominalDb != kIec60942Level114Db};
}

enum class CalibrationVerdict { Pass, Fail };

/// One calibration check: the calibrator's stated level, what the Z path
/// measured, the offset that zeroes the two (§8's `L_cal - L_meas`, ==
/// `rta::meter::calibrationOffsetDb`), and when it happened. The clock is the
/// caller's -- never an input to any mean, the same rule record §2 states for
/// the block clock itself.
struct CalibrationCheck {
    CalibrationLevel level;
    double measuredLevelDb = 0.0;
    double offsetDb = 0.0;
    std::uint64_t unixMs = 0;
};

/// §9 item 3's whole report row, read out as one plain struct so the
/// renderer (Wave 4a; deferred here because `SplReport.cpp` does not exist
/// yet -- W3-C) never has to reach back into `CalibrationSession` for one
/// more field later.
struct CalibrationReportFields {
    bool performed = false;
    CalibrationCheck start;
    CalibrationCheck end;
    double driftDb = 0.0;
    CalibrationVerdict verdict = CalibrationVerdict::Pass;
    std::string_view clause;
};

/// §8 -- calibration is a FLOW, not a stored number: a start check, an end
/// check, the drift between them, and the one published criterion this
/// project has to compare it against.
///
/// ISO 1996-2:2017 clause 5.2 requires a class 1 IEC 60942 calibrator check
/// at the BEGINNING and the END of every measurement, requires the
/// difference between two consecutive checks with no adjustment between them
/// to be <= 0.5 dB, and requires that all results since the previous
/// satisfactory check be discarded if it is exceeded. This project takes the
/// first two requirements and refuses the third (A3): the log is KEPT,
/// flagged, and the operator decides -- an instrument that discards evidence
/// on its own authority is worse than one that says why the evidence is
/// doubtful.
class CalibrationSession {
public:
    static constexpr double kMaxDriftDb = 0.5;
    static constexpr std::string_view kClause = "ISO 1996-2:2017 cl. 5.2";

    /// The ONE comparison A2's verdict rests on, factored out so the exact
    /// boundary (driftDb == kMaxDriftDb, which no log10-derived fixture can
    /// hit bit-exactly) is testable directly against `std::nextafter`
    /// rather than only through a measurement several tenths away from it.
    [[nodiscard]] static constexpr CalibrationVerdict verdictForDrift(double driftDb) noexcept {
        return driftDb <= kMaxDriftDb ? CalibrationVerdict::Pass : CalibrationVerdict::Fail;
    }

    /// Measures `samples` through the SAME Z-weighted chain a session would
    /// use (`SplMeter`, `WeightingType::Z`, never `rta::meter::Leq` --
    /// SPL-R3) and records it as the pre-check. A caller supplies whatever
    /// capture length it already has; the measurement closes exactly one
    /// block over the whole span (see the .cpp). An empty span or a
    /// non-positive `sampleRate` leaves the session unchanged.
    void recordStartCheck(CalibrationLevel level, std::span<const float> samples,
                          double sampleRate, std::uint64_t unixMs) noexcept;

    /// The same measurement, recorded as the end-of-session check. Applying
    /// no adjustment in between is the CALLER's discipline (§8: "let it
    /// settle... measure... apply it"; this never re-zeroes anything).
    void recordEndCheck(CalibrationLevel level, std::span<const float> samples,
                        double sampleRate, std::uint64_t unixMs) noexcept;

    [[nodiscard]] bool hasStartCheck() const noexcept { return startCheck_.has_value(); }
    [[nodiscard]] bool hasEndCheck() const noexcept { return endCheck_.has_value(); }
    /// Precondition: hasStartCheck() / hasEndCheck().
    [[nodiscard]] const CalibrationCheck& startCheck() const noexcept { return *startCheck_; }
    [[nodiscard]] const CalibrationCheck& endCheck() const noexcept { return *endCheck_; }

    /// `L_cal - L_meas` from the START check -- what `SplConfig::
    /// referenceOffsetDb` is set to before a session starts (record §10:
    /// fixed at session start, never re-read from a later check).
    /// Precondition: hasStartCheck().
    [[nodiscard]] double referenceOffsetDb() const noexcept { return startCheck_->offsetDb; }

    /// `|offsetEnd - offsetStart|` -- the correction EACH check implies, not
    /// the raw reading, so the number stays meaningful even if the two
    /// checks named different nominal levels. When both name the same level
    /// (the normal case) this equals `|endMeasured - startMeasured|`, ISO
    /// 1996-2 cl. 5.2's own quantity. Absent until both checks exist: there
    /// is nothing to compare yet.
    [[nodiscard]] std::optional<double> driftDb() const noexcept;

    /// Pass iff `driftDb() <= kMaxDriftDb`. Absent until both checks exist --
    /// never a default Pass for a comparison that has not happened yet
    /// (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
    [[nodiscard]] std::optional<CalibrationVerdict> verdict() const noexcept;

    /// §9 item 3's row, in one call. `performed` is false, and every other
    /// field default-constructed, until both checks exist.
    [[nodiscard]] CalibrationReportFields reportFields() const noexcept;

private:
    std::optional<CalibrationCheck> startCheck_;
    std::optional<CalibrationCheck> endCheck_;
};

}  // namespace rta::measure
