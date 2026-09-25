// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Split out of Snapshot.h (station-4 fix round, PR #31): the SPL half of a
// publish pushed that file to 407 lines, over the project's 400-line hard
// cap. No behaviour change -- these four types moved verbatim; `Snapshot.h`
// includes this header and keeps `std::optional<SplBlockView> spl` as its
// own member, unchanged.
#pragma once

#include "measure/Levels.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rta::measure {

// --- Lane L6a Wave 0, task W0-C: the SPL block ---------------------------

/// One published broadband reading, already in the units a readout prints.
struct SplMetricReading {
    std::string id;
    /// The window's Leq, mean-square referenced, plus the calibration offset
    /// (see `SplBlockView::referenceOffsetDb`).
    float valueDb = static_cast<float>(kLevelFloorDb);
    /// 0..1. How much of this metric's window the buffer actually holds. A
    /// live Leq shown without saying that its window is not yet full is a
    /// number that is quietly wrong (record §9), so this always ships beside
    /// the value and is never inferred from it.
    float leqBufferFill = 0.0f;
};

// Filling is the default and is DISTINCT from Clear (record §15 A6,
// corrected): SplAlarmReading carries no fill fraction, so reporting Clear
// while the window is still filling would read as "compared, and under the
// limit" when no comparison has run yet -- the placeholder-erases-state
// trap (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
enum class SplAlarmState { Filling, Clear, Fired };

/// One configured limit, with the verdict already reached.
///
/// `state` is SERVER-computed (record §9). A client that compared `valueDb`
/// against `limitDb` itself would disagree with the log the moment the window
/// or the exclusion membership differed -- and the log is the evidence.
struct SplAlarmReading {
    std::string metricId;
    double limitDb = 0.0;
    float valueDb = static_cast<float>(kLevelFloorDb);
    /// ABSENT when the bracket is already lost -- "the window cannot be met"
    /// is a fact about the arithmetic, not a threshold (record §6, W1-C).
    std::optional<double> headroomDb;
    SplAlarmState state = SplAlarmState::Filling;
    /// The alarm's own configured window, in blocks (plan B4's payload
    /// list). Lane L6a task W2-B's `SplAlarms` is the one producer of this
    /// type; PR #26 fix round item 6 folded its own separate SplAlarmReport
    /// into this ONE type rather than keeping two near-duplicates.
    std::uint64_t windowBlocks = 0;
    /// The block index of the most recent fire-or-clear transition; absent
    /// if this alarm has never transitioned.
    std::optional<std::uint64_t> sinceBlock;
};

/// The SPL half of one publish.
///
/// NO WALL CLOCK, deliberately (SPL-R2). `blockIndex`, `blockSamples` and
/// `sampleRate` are what a reader needs, and `t_iso` is minted at the log
/// writer and at the API serialiser instead -- which is record §2's own rule
/// ("a wall clock is recorded once per block as metadata for the human, and
/// is never an input to any mean") placed where it does not break the
/// snapshot-equality property eight `rtatool_snapshot` PNGs depend on.
///
/// NO `LevelUnit` ENUM either (SPL-R5): `referenceOffsetDb` plus `calibrated`
/// say the same two-valued thing without pulling the trace vocabulary into
/// the header every consumer includes. The mapping happens once, at the log
/// header and the report.
struct SplBlockView {
    std::uint64_t blockIndex = 0;
    std::uint32_t blockSamples = 0;
    double sampleRate = 0.0;

    double referenceOffsetDb = 0.0;
    bool calibrated = false;

    std::vector<SplMetricReading> metrics;
    /// How many configured metrics the session could NOT serve, because
    /// `SplConfig::metrics` was longer than `SplConfig::kMaxMetrics`. Normally
    /// 0. Published rather than logged, because a cap nobody is told about is
    /// a silent drop, and an operator who configured eighteen readouts and got
    /// sixteen needs to see the two (PR #17 verifier defect 1).
    std::uint32_t refusedMetrics = 0;

    /// The most recent block's own held maxima and sampled C-weighted peak,
    /// offset applied. Floats, so a consumer compares them with a
    /// float-shaped tolerance.
    float maxFastDb = static_cast<float>(kLevelFloorDb);
    float maxSlowDb = static_cast<float>(kLevelFloorDb);
    float peakCDb = static_cast<float>(kLevelFloorDb);
    /// `rta::meter::BlockFlag` bitmask, carried raw so a consumer needs no
    /// core header to pass it on.
    std::uint32_t flags = 0;
    /// Samples the bus LOST before this block closed. Elapsed samples is
    /// `Sigma(blockSamples + droppedSamples)`, which is why the count rides
    /// the block rather than living only in a live counter (SPL-R1).
    std::uint32_t droppedSamples = 0;

    std::vector<SplAlarmReading> alarms;
    /// How many `SplHistory::addMarker` calls this channel's marker ring
    /// dropped because `SplHistory::kMaxMarkers` was already reached (fix
    /// round 2026-09-25, PR #29 round-3 step 4). Normally 0 -- mirrors
    /// `refusedMetrics` above: `SplHistory::overflowedMarkers()` already
    /// existed and nothing published it, so an operator whose alarms
    /// transitioned often enough to fill a 4096-marker ring had no way to
    /// see that markers past it were silently gone.
    std::uint32_t markersOverflowed = 0;
    /// Lane L6a task W2-E2a: blocks the log-writing pipeline dropped for this
    /// channel because its fixed-capacity queue was full -- the writer
    /// thread's disk I/O falling behind, never the producer waiting for room
    /// (`SplLogPipeline`'s own class comment: the producer never blocks).
    /// Normally 0. Same "counted, never silent" shape as `refusedMetrics` and
    /// `markersOverflowed` above.
    std::uint32_t logDroppedBlocks = 0;

    /// Station-4 fix round (PR #31, verifier finding 6): true once this
    /// channel's log file has ever failed to open -- an unwritable or
    /// missing directory, most often (`SplLogWriter::openFailed()`'s own
    /// comment). STICKY, same reasoning as that method: once true, always
    /// true for this session. Normally false. Same "counted, never silent"
    /// shape as `logDroppedBlocks` above, but a bool rather than a count --
    /// there is no partial credit for a file that never opened.
    bool logWriteFailed = false;

    /// How many 100 ms Ln ticks the A-weighted chain has had to drop because
    /// its fixed tick buffer filled during one `push()` call (PR #29 round-4
    /// item 3) -- `SplMeter::overflowedLnTicks()` already existed and
    /// nothing published it, the same "counted, not silent" pattern
    /// `markersOverflowed` above and `refusedMetrics` both already follow.
    /// Normally 0.
    std::uint32_t lnTicksOverflowed = 0;

    /// True when the session's `blockSeconds` fell below
    /// `SplConfig::blockSecondsBelowRecommendedFloor`'s own advisory floor
    /// (PR #29 round-4 item 4). ADVISORY ONLY -- see that method's own
    /// comment: `SplMeter`'s internal ready buffer keeps sample accounting
    /// exact regardless of `blockSeconds`, so this is a UX recommendation
    /// ("this configuration serves no real measurement purpose"), never a
    /// correctness signal, and the session runs and logs normally either
    /// way. Normally false.
    bool blockSecondsBelowRecommendedFloor = false;

    /// ABSENT, never 0.0 %. A zero dose reads as "measured, and there was no
    /// exposure"; these are absent through Wave 0 because the accumulators
    /// ship in W1-D and a placeholder for an absent result erases its state
    /// (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
    std::array<std::optional<double>, 2> dosePercent;
    std::array<std::optional<double>, 2> doseProjected;
    /// Absent when the rank falls outside the histogram's span, with the
    /// reason reported separately -- never clamped to the bottom of the span
    /// (record §5). Absent throughout Wave 0: the histogram is W1-A.
    std::array<std::optional<double>, 6> lnDb;
};

}  // namespace rta::measure
