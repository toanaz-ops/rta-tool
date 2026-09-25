// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// enforced by the measure_has_no_framework_deps ctest.
// Lane L6a task W2-E1 (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md's
// amendment "W2-E -- the wiring nobody was assigned"; record
// docs/dsp/2026-09-16-spl-pro-l6a.md §4, §5, §6, §7, §15 A2/A6).
#pragma once

#include "measure/Snapshot.h"
#include "measure/SplAlarms.h"
#include "measure/SplConfig.h"
#include "measure/SplHistory.h"

#include "rta/meter/Block.h"
#include "rta/meter/Dose.h"
#include "rta/meter/LevelHistogram.h"

#include <array>
#include <cstdint>
#include <span>

namespace rta::measure {

/// The SPL state ONE LOGGED CHANNEL accumulates ACROSS blocks: the history
/// ring (W2-A), the configured alarms (W2-B), the Ln histogram (W1-A) and the
/// two dose accumulators (W1-D). `AnalysisThread` owns one of these per
/// logged channel; this class is the whole of "what a channel remembers"
/// between one publish and the next.
///
/// ALLOCATED ONCE, AT CONSTRUCTION (`enableSplLogging` time), AND NEVER
/// AGAIN. `SplHistory`'s ring is sized from `SplConfig::logSpanSeconds` up
/// front (its own rule, W2-A); `LevelHistogram` is two fixed arrays
/// (8 008 B, record §5); each `Dose` holds three doubles; `SplAlarms`
/// reserves its vector of latches once from `SplConfig::alarms`.
/// `test_spl_channel_state.cpp`'s A1 pushes 10x the ring's own capacity
/// through `onBlockClosed` and asserts zero bytes, through the shared
/// `AllocationProbe` (W0-B0) -- never a second global `operator new`.
///
/// UPDATED ONCE PER CLOSED BLOCK, on the analysis thread, from the same
/// `rta::meter::Block` the channel's `SplSession` chain just closed -- never
/// from a `Snapshot`, which is a throttled, read-only copy for the message
/// thread (`publishIfDue` runs at 20 Hz; a block can close far faster).
/// `AnalysisThread::feedSpl` is the one caller.
///
/// FIRST CHAIN ONLY, a limitation carried up from `SplSession` rather than
/// invented here. `SplSession` keeps one chain per distinct weighting a
/// session's metrics name (W0-B); this class -- like `SplSession::
/// latestBlock`/`window(channel)` before it -- is fed the channel's FIRST
/// chain, whichever weighting that is. A session that wants A-weighted
/// alarms and dose therefore names an A-weighted metric first. The shipped
/// `SplAlarms` (W2-B) itself takes one block span for every configured
/// alarm regardless of the alarm's own `SplAlarmSpec::metricId`, so
/// per-alarm weighting routing is not implemented at any layer yet, and this
/// class does not invent it -- a discrepancy for the orchestrator, not a
/// silent re-decision.
class SplChannelState {
public:
    /// @param config      read once, here: `logSpanSeconds`/`blockSeconds`
    ///                    size the history ring, `alarms` builds
    ///                    `SplAlarms`, `dose` seeds the two accumulators,
    ///                    `histogramBaseDb()`/`lnPercents` configure Ln.
    /// @param sampleRate  hertz; must be > 0.
    SplChannelState(const SplConfig& config, double sampleRate);

    /// Feeds exactly one newly closed block: appends it to the history ring,
    /// folds its own Leq into the Ln histogram and both dose accumulators
    /// (skipped, honestly, when the block is `CalibrationInvalid` --
    /// `combineBlocks`' own exclusion rule, record §2/§15 A2, reused here
    /// rather than re-decided), then re-evaluates every configured alarm
    /// against `windowThroughThisBlock`.
    ///
    /// @param block                   the block that just closed.
    /// @param windowThroughThisBlock  the channel's own recomputable tail,
    ///     ENDING AT `block` -- i.e. what `SplSession::window(channel)`
    ///     held at the instant this block closed, never a later window that
    ///     already contains blocks after it. `AnalysisThread::feedSpl`
    ///     trims for this on the rare hop that closes more than one block.
    void onBlockClosed(const rta::meter::Block& block,
                       std::span<const rta::meter::Block> windowThroughThisBlock);

    /// Fills the alarm/dose/Ln half of a publish from the state accumulated
    /// so far. A slot this class has not yet produced a result for is left
    /// exactly as `view` arrived -- ABSENT, never a placeholder zero
    /// (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
    void fillPublish(SplBlockView& view) const;

    [[nodiscard]] const SplHistory& history() const noexcept { return history_; }
    [[nodiscard]] SplHistory& history() noexcept { return history_; }

private:
    double sampleRate_;
    double blockSeconds_;
    double referenceOffsetDb_;
    std::array<double, 6> lnPercents_;

    SplHistory history_;
    SplAlarms alarms_;
    rta::meter::LevelHistogram lnHistogram_;
    /// TWO accumulators, always (record §7): `dose_[0]`/`dose_[1]` mirror
    /// `SplConfig::dose[0]`/`[1]` index for index -- never re-ordered, so a
    /// caller reading `SplBlockView::dosePercent[i]` gets the SAME preset
    /// `SplConfig::dose[i]` named.
    std::array<rta::meter::Dose, 2> dose_;
};

}  // namespace rta::measure
