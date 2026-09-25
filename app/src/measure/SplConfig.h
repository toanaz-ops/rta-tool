// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// this file compiles into rtatool_analysis_tests, which builds on CI with
// RTA_BUILD_APP=OFF. Lane L6a task W0-B (record docs/dsp/
// 2026-09-16-spl-pro-l6a.md §2, §10, §11, §13).
#pragma once

#include "measure/Levels.h"

#include "rta/dsp/Weighting.h"
#include "rta/meter/Detector.h"
#include "rta/meter/Dose.h"

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
/// NIOSH REL, accumulator A's default.
///
/// NIOSH 98-126 cl. 1.1.1, printed p. 1: `T(min) = 480/2^((L-85)/3)`, followed
/// by the words "where 3 = the exchange rate" -- so the denominator is
/// `3/log10(2)` and NOT 10. cl. 1.3.3, printed p. 4, requires all levels
/// "from 80 to 140 dBA" to be integrated, which is where the 80 dB(A)
/// threshold comes from and why the document's own Table 1-1 starts at 80
/// rather than at the 85 dB(A) REL.
///
/// A FUNCTION rather than a constant, and that is the point: `q` has to be
/// COMPUTED by `rta::meter::exchangeDenominator`, which calls `std::log10` and
/// is therefore not usable in a constant expression. Typing the readable
/// 9.9657843 instead would be 1.53e-08 high -- undetectable by any dose
/// acceptance in this project -- but `10^(3/q)` would then no longer be
/// EXACTLY 2.0, which is what core's test_dose.cpp D1b/D1c/D1f rest on
/// (SPL-R7). A `constexpr` preset would have forced the literal.
///
/// OWNER DECISION NOT MADE, recorded rather than resolved: 98-126 contains
/// TWO tables needing two different exchange constants. Table 1-1 agrees with
/// the formula above; Table 1-2's own printed footnote is
/// `TWA = 10 x Log(D/100) + 85`, i.e. `q = 10` exactly, and its last row
/// (32,500,000 % -> 140.1 dBA) proves it. The gap reaches 4.2549 % of dose at
/// 140 dB(A). This preset ships the value that reproduces Table 1-1, because
/// Table 1-1 is the artefact an inspector reads. Record section 13 Q4.
[[nodiscard]] inline rta::meter::DoseSettings nioshRelDose() noexcept {
    rta::meter::DoseSettings s;
    s.criterionLevelDb = 85.0;
    s.criterionSeconds = 8.0 * 3600.0;
    s.q = rta::meter::exchangeDenominator(3.0);
    s.thresholdDb = 80.0;
    return s;
}

/// OSHA PEL, accumulator B's default.
///
/// 29 CFR 1910.95(a) requires A weighting and SLOW. The dose is computed
/// against Table G-16a per Appendix A (mandatory) I(1)(i) -- NOT against
/// Table G-16, which is the body's permissible-exposure table -- and that
/// table's footnote formula is `T = 8/2^((L-90)/5)`, so the denominator is
/// `5/log10(2)`. Appendix A I(2) gives `TWA = 16.61 log10(D/100) + 90`, the
/// same denominator rounded for print.
///
/// The 90 dB(A) threshold is the PEL dose. The hearing-conservation dose uses
/// 80 dB(A); that is a second CONFIGURATION of the same accumulator, not a
/// third accumulator, and which one an operator wants is their setting.
[[nodiscard]] inline rta::meter::DoseSettings oshaPelDose() noexcept {
    rta::meter::DoseSettings s;
    s.criterionLevelDb = 90.0;
    s.criterionSeconds = 8.0 * 3600.0;
    s.q = rta::meter::exchangeDenominator(5.0);
    s.thresholdDb = 90.0;
    return s;
}

struct SplConfig {
    /// THE ONE CAP ON `metrics`, and it is here rather than in the publish
    /// path on purpose (PR #17 verifier defect 1).
    ///
    /// `metrics` was an unbounded vector validated nowhere, while the publish
    /// path's per-metric window storage is a fixed array -- so a 17-metric
    /// config overflowed the array's capacity, `fillMetricWindows` gave up
    /// all-or-nothing, and every metric silently fell back to the first
    /// weighting's chain: a C-weighted metric published A-weighted numbers
    /// under a C label, 18.8 dB wrong. The cap lives with the data it bounds
    /// so the number cannot drift from the storage it sizes;
    /// `AnalysisThread`'s array is declared from THIS constant.
    ///
    /// Sixteen is generous against the six-slot Ln shape §13 Q3 ships and the
    /// handful of broadband metrics a show actually reads. Raising it is a
    /// one-line change here, and the array follows automatically.
    static constexpr std::size_t kMaxMetrics = 16;

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

    /// TWO dose accumulators, always, each with its own four settings
    /// (record section 7). Not a convenience: the Larson Davis 831/LxT
    /// defines `NUM_SLM_DOSES = 2` with a per-dose {threshold, exchange rate,
    /// criterion time, criterion level}, and it is what makes Smaart SPL's
    /// `Exposure O` and `Exposure N` columns exist side by side in one log. A
    /// single dose setting cannot produce that log, and it forces a wrong
    /// threshold onto whichever preset loses.
    ///
    /// The presets are DATA here and named nowhere in core (record
    /// section 11): core holds the formula and four numbers, app holds which
    /// four. `core/tests/test_dose.cpp` D3b greps core for these names; this
    /// file is where they are allowed to be.
    std::array<rta::meter::DoseSettings, 2> dose{{nioshRelDose(), oshaPelDose()}};

    /// How many of `metrics` a session can actually serve, and how many it
    /// cannot. A caller that wants to refuse rather than truncate checks
    /// `refusedMetricCount() != 0` before starting a session; `SplSession`
    /// itself truncates and REPORTS, which is the default this lane takes
    /// (see `SplSession::refusedMetrics`).
    [[nodiscard]] std::size_t acceptedMetricCount() const noexcept {
        return metrics.size() < kMaxMetrics ? metrics.size() : kMaxMetrics;
    }
    [[nodiscard]] std::size_t refusedMetricCount() const noexcept {
        return metrics.size() - acceptedMetricCount();
    }

    /// PR #29 round-3 fix pass step 2's defensive sanity gate: true when
    /// `blockSeconds` is so small, relative to `scratchSamples`/
    /// `readyCapacity`, that more than `readyCapacity` blocks could
    /// complete within a single scratch-sized segment window
    /// (`SplMeter::kScratchSamples` / `rta::meter::BlockAccumulator::
    /// kReadyCapacity` -- passed in rather than named here so this header
    /// stays SplMeter-free). `SplSession::start` checks this and reports it
    /// through `blockSecondsTooSmall()`, NEVER silently, mirroring
    /// `refusedMetricCount()`'s own pattern -- but it does NOT refuse to
    /// start: `SplMeter`'s own internal ready buffer (SplMeter.h's
    /// `kReadyBufferCapacity`) is independently sized at `kScratchSamples`
    /// (1024) and keeps every sample counted (Sigma(blockSamples +
    /// droppedSamples) == total pushed) regardless of this gate. This is a
    /// SEPARATE, purely advisory floor: a `blockSeconds` this far under one
    /// scratch chunk serves no real measurement purpose either way, and an
    /// operator who typed one gets told rather than left to wonder why the
    /// block clock looks odd.
    ///
    /// WHY ADVISORY AND NOT A REFUSAL (round-4 item 4, spelled out because a
    /// future reader will be tempted to make it a hard gate): correctness
    /// does not depend on this number. Sample accounting is EXACT for any
    /// `blockSeconds` >= one sample, proven directly by
    /// test_spl_meter.cpp's own Sigma(blockSamples+droppedSamples) case and
    /// by the round-4 item 2 eviction fix -- this floor is a UX
    /// RECOMMENDATION ("this configuration is finer than any real
    /// measurement needs and may just be a typo"), never a correctness
    /// requirement, and refusing to start a session over a UX opinion would
    /// lose a show's evidence for no measurement reason at all. Published on
    /// `SplBlockView::blockSecondsBelowRecommendedFloor` (round-4 item 4) so
    /// the operator is told rather than left to wonder.
    [[nodiscard]] bool blockSecondsBelowRecommendedFloor(double sampleRate, double scratchSamples,
                                                          double readyCapacity) const noexcept {
        if (!(sampleRate > 0.0) || !(blockSeconds > 0.0) || !(readyCapacity > 0.0)) return true;
        return blockSeconds * sampleRate < (scratchSamples / readyCapacity);
    }

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
