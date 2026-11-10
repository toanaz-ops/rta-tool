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
    /// Centre of the offset search, samples: the MEASUREMENT index of reference sample 0, which is
    /// T_ref - T_meas from the two bext time references when both files carry one, else 0 (record E2;
    /// `bextPriorSamples`).
    std::int64_t priorOffsetSamples = 0;
    /// The search covers prior +- this many seconds. 10 s is the production
    /// default; a test may narrow it only to keep a fixture small.
    double coarseHalfWidthSeconds = 10.0;
    /// |ppm| above this is refused (record E4). 400: MEASURED on the two-file
    /// fixture, not the record's 1000 (sweep table in
    /// app/tests/test_wav_pair_transfer_limits.cpp). With the coarse and pass-1
    /// GCC-PHAT and phase-slope fit band-limited (see `detail::driftBand`) all 9
    /// pass-1 windows resolve up to 400 ppm; at 600 the 2^17 coarse window smears
    /// 79 samples and the pair is refused as NoCoarseAlignment (it cannot be
    /// measured). DriftTooLarge is what a pair the search CAN still measure
    /// (400..~500 ppm) is refused with.
    double maxPpm = 400.0;
};

enum class ClockRefusal {
    NoCoarseAlignment,  ///< E2: no Accepted peak inside prior +- half-width
    NoCorrelation,      ///< E3/E6: fewer than 3 windows were Accepted
    DriftTooLarge,      ///< E4
    DriftNotLinear,     ///< E6
    RangeTooShort,      ///< shorter than 2^17 samples: four pass-1 windows spread over at least 3/4 of it
    RangeTooLong,       ///< the ppm margin of the pass-1 lag search would pass a quarter of a window
    RangeOutsideOverlap,  ///< the range does not fit where the two files overlap (coarse offset +- the ppm margin, then the pass-1 mapping)
    CoarseSpanTooShort,   ///< E2: the measurement leaves less than a coarse window to search (too short, or the prior points outside it)
    ClockUncertain,       ///< the fitted drift is not known to 0.1 sample over the range (noise, not non-linearity)
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
    double uncertaintySamples = 0.0;  ///< the predicted total drift error over the range (limit 0.1)
    std::int64_t rangeLength = 0;  ///< the range analysed, samples (the resolved one when LENGTH was 0)
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
    /// RangeTooShort with LENGTH empty: what both files cover after START, seconds (0 = the typed range was short).
    double overlapAfterStartSeconds = 0.0;
    std::int64_t overlapFirst = 0;  ///< RangeOutsideOverlap: first/last reference sample both files cover
    std::int64_t overlapLast = -1;
    double maxRangeMinutes = 0.0;   ///< RangeTooLong
    double uncertaintySamples = 0.0;  ///< ClockUncertain: predicted total drift error over the range
    double uncertaintyLimit = 0.0;
    double coarseSpanSeconds = 0.0;   ///< CoarseSpanTooShort: what the search could use ...
    double coarseNeededSeconds = 0.0;  ///< ... what it needs,
    double priorSeconds = 0.0;         ///< the prior offset
    double measurementSeconds = 0.0;   ///< and the measurement file's length

    [[nodiscard]] bool ok() const noexcept { return estimate.has_value(); }
};

/// `range*` are on the reference file's timeline. `rangeLength` 0 = to the end of
/// the part both files cover (the overlap, less the ppm margin). Blocks (seconds on a long
/// range): run it on a worker; `cancel` is polled between windows.
[[nodiscard]] ClockResult estimateClockMapping(const SampleBlockSource& reference, int referenceChannel,
                                               const SampleBlockSource& measurement,
                                               int measurementChannel, std::int64_t rangeStart,
                                               std::int64_t rangeLength,
                                               const ClockSearchOptions& options,
                                               const std::atomic<bool>& cancel);

}  // namespace rta::measure
