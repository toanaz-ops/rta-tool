// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE.
//
// H12 T3 (docs/dsp/2026-11-10-g22b-two-file-wav-drift.md E2-E7): where the
// measurement file's clock sits against the reference file's -- an offset and a
// ratio -- found by a coarse correlation, then two passes of windowed
// delay measurements fitted with a Theil-Sen line.
#pragma once

#include "measure/ResampledPairSource.h"

#include <atomic>
#include <cstdint>
#include <optional>

namespace rta::measure {

struct ClockSearchOptions {
    /// Centre of the offset search, samples (meas minus ref); the bext difference
    /// when both files carry one, else 0 (record E2).
    std::int64_t priorOffsetSamples = 0;
    /// The search covers prior +- this many seconds. 10 s is the production
    /// default; a test may narrow it only to keep a fixture small.
    double coarseHalfWidthSeconds = 10.0;
    /// |ppm| above this is refused (record E4). 100: MEASURED on the two-file
    /// fixture, not the record's 1000 (sweep table in
    /// app/tests/test_wav_pair_transfer_limits.cpp). At 100 ppm all 9 pass-1 windows
    /// resolve; at 125 only 7, at 150 only 3, at 175 none: the clock drifts 4-6
    /// samples INSIDE one 2^15 window and the phase-slope fit loses the coherence it
    /// needs (fewer than 8 bins with gamma^2 >= 0.5). A pair beyond ~170 ppm is
    /// therefore refused as NoCorrelation / NoCoarseAlignment (it cannot be
    /// measured), and DriftTooLarge is what a pair the windows CAN still measure,
    /// 100..~150 ppm, is refused with. Raising the limit without widening that
    /// capture range only invites thin fits (3 windows of 9).
    double maxPpm = 100.0;
};

enum class ClockRefusal {
    NoCoarseAlignment,  ///< E2: no Accepted peak inside prior +- half-width
    NoCorrelation,      ///< E3/E6: fewer than 3 windows were Accepted
    DriftTooLarge,      ///< E4
    DriftNotLinear,     ///< E6
    RangeTooShort,      ///< shorter than one pass-1 window
    ReadFailed,
    Cancelled,
};

struct ClockEstimate {
    ClockMapping mapping;
    bool sharedClock = false;      ///< E7: no resampling, integer offset only
    double ppm = 0.0;              ///< (ratio - 1) * 1e6, 0 when shared
    double offsetSeconds = 0.0;    ///< t(rangeStart) - rangeStart, in seconds
    double residualRms = 0.0;      ///< pass-2 residual RMS, samples (0 when shared)
    double maxResidual = 0.0;
    int windowsUsed = 0;           ///< pass-1 windows that were Accepted
};

/// Either an estimate or a refusal plus the measured numbers its sentence prints.
struct ClockResult {
    std::optional<ClockEstimate> estimate;
    std::optional<ClockRefusal> refusal;

    // NoCoarseAlignment / NoCorrelation: the best trust seen and the trust it needed.
    double bestTrust = 0.0;
    double trustFloor = 0.0;
    int windowsAccepted = 0;       ///< NoCorrelation
    int windowsNeeded = 0;
    double coverage = 1.0;         ///< NoCorrelation: fraction of the range the accepted windows span
    // DriftTooLarge: the slope found and the limit.
    double ppm = 0.0;
    double maxPpm = 0.0;
    double halfWidthSeconds = 0.0; ///< NoCoarseAlignment: the half-width searched
    // DriftNotLinear: pass-2 residual, samples.
    double residualRms = 0.0;
    double maxResidual = 0.0;
    double minRangeSeconds = 0.0;  ///< RangeTooShort

    [[nodiscard]] bool ok() const noexcept { return estimate.has_value(); }
};

/// `range*` are on the reference file's timeline. Blocks (seconds on a long
/// range): run it on a worker; `cancel` is polled between windows.
[[nodiscard]] ClockResult estimateClockMapping(const SampleBlockSource& reference, int referenceChannel,
                                               const SampleBlockSource& measurement,
                                               int measurementChannel, std::int64_t rangeStart,
                                               std::int64_t rangeLength,
                                               const ClockSearchOptions& options,
                                               const std::atomic<bool>& cancel);

}  // namespace rta::measure
