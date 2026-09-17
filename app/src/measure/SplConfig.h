// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// this file compiles into rtatool_analysis_tests, which builds on CI with
// RTA_BUILD_APP=OFF. Lane L6a task W0-B (record docs/dsp/
// 2026-09-16-spl-pro-l6a.md §2, §10, §11, §13).
#pragma once

#include "measure/Levels.h"

#include "rta/dsp/Weighting.h"
#include "rta/meter/Detector.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rta::measure {

/// One published broadband reading: a weighting, a detector and how many
/// blocks the window it is averaged over spans.
struct SplMetricSpec {
    std::string id;
    rta::dsp::WeightingType weighting = rta::dsp::WeightingType::A;
    rta::meter::TimeWeighting detector = rta::meter::TimeWeighting::Fast;
    std::uint64_t windowBlocks = 1;
};

/// A limit on a named metric over a named window. The comparison itself is
/// `rta::meter::AlarmLatch`'s (W1-C); this is only the configuration.
struct SplAlarmSpec {
    std::string metricId;
    double limitDb = 0.0;
    std::uint64_t windowBlocks = 1;
};

/// Everything a logging session is configured with, fixed at session start.
///
/// Record §10 forbids a setting changing mid-log, so every derived quantity
/// below is computed once from these values and never re-read while blocks are
/// being written. None of these are persisted in v1: there is no preferences
/// store anywhere under `app/src` (SPL-R11), so the composition root
/// constructs them from these defaults.
struct SplConfig {
    /// Record §2's default, and settable. 1 s divides 3 s / 60 s / 5 min /
    /// 60 min, which is what makes every window in §3 a whole number of
    /// blocks.
    double blockSeconds = 1.0;

    /// DATA, not a flow. Wave 3 sets it from a calibration check; without
    /// Wave 3 the operator types it. Waves 0-2 take it as a number precisely
    /// so that cutting Wave 3 is a deletion and not a rewrite (record §13 Q2).
    double referenceOffsetDb = 0.0;

    /// SPL-R5: `rta::trace::LevelUnit` is NOT pulled into the snapshot
    /// vocabulary for the sake of one enum. The mapping from this bool to that
    /// enum happens once, at the log header and the report.
    bool calibrated = false;

    /// Record §13 Q7's defaults: eight hours is "a show plus load-in", and a
    /// segment is one hour of blocks -- a number an operator can state, rather
    /// than a byte count. The app never deletes.
    double logSpanSeconds = 8 * 3600.0;
    std::uint64_t segmentBlocks = 3600;

    /// DERIVED, never a free constant (defect 4;
    /// memory/a-default-must-be-run-through-the-gate-it-feeds.md).
    ///
    /// Record §5 wrote `-20.0`, which is right ONCE AN OFFSET EXISTS. The Q1
    /// scope default shipped beside it puts UNCALIBRATED SPL in mean-square
    /// dBFS, where a real session sits at -30..-60 dBFS -- so a hard
    /// `[-20, +180)` span would make every Ln of every out-of-the-box session
    /// permanently `BelowSpan`: a default that guarantees its own gate fails.
    ///
    /// Uncalibrated the derived span is `[-120, +80)`, which contains every
    /// reading the Q1 default can produce: a full-scale sine at 0.0 dBFS lands
    /// at bin 1200 of 2000, 60 % up, with 80 dB of headroom above it. At a
    /// typical `+100 dB` offset it becomes `[-20, +180)` -- record §5's own
    /// number RECOVERED rather than contradicted, and a 140 dB(A) peak lands
    /// at bin 1600.
    [[nodiscard]] double histogramBaseDb() const noexcept {
        return kLevelFloorDb + referenceOffsetDb;
    }

    /// Record §13 Q3's default, the Larson Davis `NUM_LNS = 6` shape. The
    /// percentages are settable; the count is not.
    std::array<double, 6> lnPercents{1.0, 5.0, 10.0, 50.0, 90.0, 95.0};

    std::vector<SplMetricSpec> metrics;
    std::vector<SplAlarmSpec> alarms;

    // DEVIATION FROM THE PLAN'S API SKETCH, NAMED: the plan lists
    // `std::array<rta::meter::DoseSettings, 2> dose` here. `DoseSettings`
    // ships in W1-D (core/include/rta/meter/Dose.h), which is Wave 1, so the
    // field cannot exist yet without inventing the type in the wrong lane.
    // It is added by W1-D's own task, where the two accumulators it names are
    // also built. Nothing in Wave 0 reads a dose.

    /// Blocks in `windowSeconds` of this configuration -- the one place a
    /// duration becomes a block count, so a caller never divides by
    /// `blockSeconds` itself and rounds differently.
    [[nodiscard]] std::uint64_t windowBlocks(double windowSeconds) const noexcept {
        if (!(blockSeconds > 0.0) || !(windowSeconds > 0.0)) return 0;
        const double blocks = windowSeconds / blockSeconds;
        return static_cast<std::uint64_t>(blocks + 0.5);
    }
};

}  // namespace rta::measure
