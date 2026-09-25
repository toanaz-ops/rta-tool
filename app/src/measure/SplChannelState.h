// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// enforced by the measure_has_no_framework_deps ctest.
// Lane L6a task W2-E1 (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md's
// amendment "W2-E -- the wiring nobody was assigned"; record
// docs/dsp/2026-09-16-spl-pro-l6a.md §4, §5, §6, §7, §15 A2/A6). Fix round
// 2026-09-25: an independent verifier refuted the first version of this
// file -- every consumer read the channel's FIRST configured chain
// regardless of which metric it was actually about
// (memory/a-config-field-with-one-value-in-every-fixture.md). See each
// member's own comment for what changed.
#pragma once

#include "measure/Snapshot.h"
#include "measure/SplAlarms.h"
#include "measure/SplConfig.h"
#include "measure/SplHistory.h"

#include "rta/dsp/Weighting.h"
#include "rta/meter/Block.h"
#include "rta/meter/Dose.h"
#include "rta/meter/LevelHistogram.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rta::measure {

/// The window ENDING AT `closed[i]`, reconstructed from `closed` (one
/// chain's own newly-closed blocks for the current hop, oldest first) and
/// `fullWindow` (that same chain's window immediately AFTER the hop, i.e.
/// `SplSession::window(channel, weighting)` read right after `feedHop`).
///
/// Fix round 2026-09-25, verifier D3: the naive
/// `fullWindow.first(fullWindow.size() - trim)` underflows (unsigned
/// wraparound, `std::span::first` then reading past the end) whenever one
/// hop closes MORE blocks than the window retains -- repro: `blockSeconds =
/// 0.005`, `windowBlocks = 1`, a 1024-sample hop closes 4 blocks against a
/// 1-block window.
///
///   COMMON CASE (`closed.size() <= fullWindow.size()`): the window
///   comfortably outlives one hop's batch, so its own history from BEFORE
///   this batch is still the prefix of `fullWindow` that the later members
///   of `closed` have not yet rolled past -- the original trim arithmetic,
///   proven safe because `fullWindow.size() >= closed.size() > i` here.
///
///   RARE (`closed.size() > fullWindow.size()`): this hop alone closed more
///   blocks than the window retains, so `fullWindow` has already rolled
///   ENTIRELY within this batch and retains NOTHING from before it. Every
///   intermediate window here is reconstructed from `closed` alone -- the
///   best available answer, and it under-fills (never over-fills, never
///   underflows) exactly where the true pre-batch history is no longer
///   retrievable from a single-window design.
[[nodiscard]] std::span<const rta::meter::Block> windowAtClose(
    std::span<const rta::meter::Block> closed, std::span<const rta::meter::Block> fullWindow,
    std::size_t i) noexcept;

/// One chain's newly-closed block for the current hop, with the window
/// ending at it -- what `AnalysisThread::feedSpl` (via `windowAtClose`
/// above) builds per weighting and hands to `SplChannelState::onBlockClosed`
/// so every consumer can read the SPECIFIC chain its own definition names.
struct ChainBlockAtClose {
    rta::dsp::WeightingType weighting = rta::dsp::WeightingType::Z;
    rta::meter::Block block;
    std::span<const rta::meter::Block> windowThroughThisBlock;
};

/// The SPL state ONE LOGGED CHANNEL accumulates ACROSS blocks: the history
/// ring (W2-A), the configured alarms (W2-B), the Ln histogram (W1-A) and the
/// two dose accumulators (W1-D). `AnalysisThread` owns one of these per
/// logged channel; this class is the whole of "what a channel remembers"
/// between one publish and the next.
///
/// EVERY CONSUMER READS THE CHAIN ITS OWN DEFINITION NAMES, never a
/// substitute (fix round 2026-09-25):
///   - an alarm reads the chain of the metric its `metricId` names;
///   - dose reads the A-weighted chain (record §7: NIOSH and OSHA are both
///     defined in dBA) -- and `SplSession::start` now GUARANTEES that chain
///     exists, auto-created the same way it already auto-creates a Z chain
///     when no metric names one, because dose is always configured
///     (`SplConfig::dose` carries defaults, never an on/off flag);
///   - the Ln histogram reads the same A-weighted chain, Fast detector
///     (`Block::maxFastDb`) -- record §5's own label convention, `L_AF...`.
/// `onBlockClosed` is therefore handed EVERY configured chain's own closed
/// block and window for this hop, not one shared pair.
///
/// AN ALARM WHOSE `metricId` NAMES NO CONFIGURED METRIC IS PUBLISHED
/// ABSENT, not refused at construction. Its `SplAlarmReading` still appears
/// in `SplBlockView::alarms` (so an operator can see the alarm exists in the
/// config) but is never fed, so it stays at its constructed default
/// (`Filling`, no headroom) for the life of the session -- honest, because
/// nothing was ever compared, and it needs no new field on `SplBlockView`
/// the way a refused-count would (mirroring `SplConfig::refusedMetricCount`
/// would cost a wire-format change this task's scope does not open).
///
/// ALLOCATED ONCE, AT CONSTRUCTION (`enableSplLogging` time), AND NEVER
/// AGAIN. `SplHistory`'s ring and marker store are sized up front (W2-A,
/// and fix round 2026-09-25's `SplHistory::kMaxMarkers`); `LevelHistogram`
/// is two fixed arrays (8 008 B, record §5); each `Dose` holds three
/// doubles; every `SplAlarms` group reserves its vector of latches once,
/// partitioned by weighting at construction. `test_spl_channel_state.cpp`'s
/// allocation-probe fixture pushes many blocks AND drives alarm Fired/
/// Cleared transitions through `onBlockClosed` and asserts zero bytes,
/// through the shared `AllocationProbe` (W0-B0) -- never a second global
/// `operator new`.
///
/// UPDATED ONCE PER CLOSED BLOCK, on the analysis thread, from the same
/// `rta::meter::Block` each chain's `SplSession` chain just closed -- never
/// from a `Snapshot`, which is a throttled, read-only copy for the message
/// thread (`publishIfDue` runs at 20 Hz; a block can close far faster).
/// `AnalysisThread::feedSpl` is the one caller.
class SplChannelState {
public:
    /// @param config      read once, here: `logSpanSeconds`/`blockSeconds`
    ///                    size the history ring, `alarms` (partitioned by
    ///                    each alarm's own metric's weighting) builds the
    ///                    `SplAlarms` groups, `dose` seeds the two
    ///                    accumulators, `histogramBaseDb()`/`lnPercents`
    ///                    configure Ln.
    /// @param sampleRate  hertz; must be > 0.
    SplChannelState(const SplConfig& config, double sampleRate);

    /// Feeds every chain's newly closed block for ONE hop's closing event:
    /// appends the FIRST configured chain's block to the history ring
    /// (unchanged convention, not flagged by the fix round -- History is a
    /// generic per-channel display ring, not tied to one metric); folds the
    /// A-weighted chain's block into the Ln histogram and both dose
    /// accumulators when that chain is present (skipped, honestly, when the
    /// block is `CalibrationInvalid` -- `combineBlocks`' own exclusion rule,
    /// record §2/§15 A2, reused here rather than re-decided); then
    /// re-evaluates every alarm group against ITS OWN weighting's window,
    /// when that weighting is present in `chains`.
    ///
    /// @param chains  every configured chain's own closed block and window
    ///     for this hop, in `SplSession::weightings()` order. Empty is a
    ///     no-op.
    void onBlockClosed(std::span<const ChainBlockAtClose> chains);

    /// Fills the alarm/dose/Ln half of a publish from the state accumulated
    /// so far. A slot this class has not yet produced a result for is left
    /// exactly as `view` arrived -- ABSENT, never a placeholder zero
    /// (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
    void fillPublish(SplBlockView& view) const;

    [[nodiscard]] const SplHistory& history() const noexcept { return history_; }
    [[nodiscard]] SplHistory& history() noexcept { return history_; }

    /// One `SplAlarms` per DISTINCT weighting an alarm's own metric names,
    /// built once at construction by partitioning `SplConfig::alarms`
    /// (never touched after). `weighting` is absent for the group holding
    /// every alarm whose `metricId` matched no configured metric -- that
    /// group is never fed (see the class's own "ABSENT, not refused"
    /// comment), and `onBlockClosed` skips it by construction (no `chains`
    /// entry can ever match an absent weighting). Public only so
    /// SplChannelState.cpp's anonymous-namespace `buildAlarmGroups` helper
    /// can name it from outside the class -- a friend declaration cannot
    /// reach into an anonymous namespace reliably across the header/source
    /// split, and nothing else has reason to touch it.
    struct AlarmGroup {
        std::optional<rta::dsp::WeightingType> weighting;
        SplAlarms alarms;
    };

private:
    double sampleRate_;
    double blockSeconds_;
    double referenceOffsetDb_;
    std::array<double, 6> lnPercents_;

    SplHistory history_;
    std::vector<AlarmGroup> alarmGroups_;
    rta::meter::LevelHistogram lnHistogram_;
    /// TWO accumulators, always (record §7): `dose_[0]`/`dose_[1]` mirror
    /// `SplConfig::dose[0]`/`[1]` index for index -- never re-ordered, so a
    /// caller reading `SplBlockView::dosePercent[i]` gets the SAME preset
    /// `SplConfig::dose[i]` named.
    std::array<rta::meter::Dose, 2> dose_;
};

}  // namespace rta::measure
