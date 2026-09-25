// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Lane L6a task W0-D (research §C4, SPL-R1, SPL-R2).
#pragma once

#include "measure/SplConfig.h"
#include "measure/SplMeter.h"

#include "rta/meter/Block.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace rta::measure {

/// One live SPL logging session: an `SplMeter` per logged channel, the window
/// each metric is recomputed over, and the bus drop-count baseline that turns
/// a stalled drain into a `Gap` WITH A LENGTH.
///
/// WHY THIS IS ITS OWN FILE (deviation from the plan's file list, named).
/// W0-D's task lists `AnalysisThread.h/.cpp` only, with the escape hatch "if
/// it breaches, the seam is drain-versus-feed and the feed moves out". It
/// does breach: `AnalysisThread.cpp` is 365 lines against a 400 hard cap and
/// the session state is about 50. So the feed moved, and it moved HERE rather
/// than into `AnalysisPublish.cpp` because this is STATE with a lifetime,
/// while that file is free functions over objects the caller owns. The win is
/// the same one the plan wanted and one more besides: this file is JUCE-free,
/// so the whole session -- the block clock, the gap arithmetic, the window --
/// is provable with `RTA_BUILD_APP=OFF` on all three CI operating systems,
/// and `AnalysisThread` keeps only the three lines that are genuinely about
/// the drain.
///
/// THREADING. Analysis-thread-only, and not synchronised: `start`/`stop`
/// allocate and are the message thread's, so the caller is responsible for
/// handing them over (`AnalysisThread` does it with the same
/// atomic-request-flag shape `armLocateCapture` already uses). Every accessor
/// below returns a plain value read on the analysis thread; publishing them
/// across a thread boundary is the caller's job, exactly as `routeHopCounts_`
/// already is.
///
/// NOT A SECOND RING READER. `feedHop` takes the scratch buffer the drain
/// already filled. `rta::dsp::RingBuffer` has ONE `readIndex_` and
/// `AnalysisThread::drainPaired` owns it (record §0 hazard 2); a second
/// cursor is a `platform/` change and is explicitly not built here.
class SplSession {
public:
    /// Channels are indexed directly, like `AnalysisThread::channelScratch_`.
    /// The SPL meters are NOT `analysers_` and are NOT bounded by
    /// `kMaxTransferFunctions` -- that constant is an MTW-memory policy
    /// (RoutingPlan.h:25-29), not a channel-count limit, and a route past it
    /// must still log (W0-D D1b).
    static constexpr std::size_t kMaxLoggedChannels = 64;

    /// Wave 0's window cap. The real ring, sized once from
    /// `SplConfig::logSpanSeconds` and never grown, is W2-A; until then a
    /// window is capped at the longest metric's own `windowBlocks` and at
    /// this, so a long session cannot grow without bound.
    static constexpr std::size_t kMaxWindowBlocks = 3600;

    SplSession() = default;

    /// Begins a session on `channels`. ALLOCATES -- the chains and the
    /// windows are built here and nothing after. A channel outside
    /// [0, kMaxLoggedChannels) is ignored rather than clamped onto a
    /// neighbour's slot. A config with no metrics gets one Z chain, so a
    /// caller that only wants unweighted energy is not a special case
    /// everywhere else.
    ///
    /// AN A-WEIGHTED CHAIN ALWAYS EXISTS, auto-created here exactly like the
    /// Z chain above when no metric already names one (fix round
    /// 2026-09-25, orchestrator refinement). Dose (record §7) and the Ln
    /// histogram (record §5) are both defined in dBA and both always
    /// configured -- `SplConfig::dose` and `::lnPercents` carry defaults,
    /// never an on/off flag -- so a correct A-weighted figure is worth more
    /// than an absent one, and the chain costs one extra weighting filter.
    /// This auto-chain is NOT a metric: it never touches `config_.metrics`,
    /// `refusedMetrics_` or `SplConfig::kMaxMetrics`'s count, the same
    /// exemption the Z chain already has.
    ///
    /// `config.metrics` is TRUNCATED to `SplConfig::kMaxMetrics` and the
    /// number dropped is reported by `refusedMetrics()` below and published on
    /// `SplBlockView::refusedMetrics` (PR #17 verifier defect 1: an unbounded
    /// metric list overflowed the publish path's fixed window storage and
    /// reopened the weighting defect one index above the bound).
    ///
    /// TRUNCATION RATHER THAN REFUSING THE WHOLE SESSION, and the choice is
    /// the point: a misconfiguration that silenced SPL logging outright would
    /// lose a show's evidence, which is worse than logging the first sixteen.
    /// That is only acceptable BECAUSE the count is reported -- a silent drop
    /// would be the same defect this closes, one layer further out. A caller
    /// that would rather refuse reads `SplConfig::refusedMetricCount()` before
    /// calling this.
    void start(const SplConfig& config, double sampleRate, std::span<const int> channels);

    void stop() noexcept;

    /// How many of the caller's metrics `start` dropped, or 0. Never silent:
    /// this reaches the published `SplBlockView` as well.
    [[nodiscard]] std::size_t refusedMetrics() const noexcept { return refusedMetrics_; }

    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] bool logsChannel(int channel) const noexcept;

    /// Records the bus's own CUMULATIVE dropped-sample count for `channel`
    /// (`rta::platform::CaptureBus::dropCount`). The DELTA since the last
    /// call rides the block being accumulated as `droppedSamples` and sets
    /// `BlockFlag::Gap` on it.
    ///
    /// Taken as a plain number rather than a `CaptureBus&` so this file needs
    /// no platform header and stays provable with no bus in the path.
    ///
    /// The FIRST call on a channel only establishes the baseline: a bus that
    /// has already dropped samples before logging began has not lost anything
    /// from THIS log.
    void noteDropCount(int channel, std::uint64_t busDropCount) noexcept;

    /// Feeds one hop from the scratch the drain already filled, then drains
    /// every block that completed into `channel`'s window. Allocation-free.
    void feedHop(int channel, std::span<const float> hop) noexcept;

    [[nodiscard]] std::uint64_t blockCount(int channel) const noexcept;
    /// The OR of every flag any block on this channel has carried. Monotonic,
    /// so a later clean block cannot erase a Gap that happened.
    [[nodiscard]] std::uint32_t flagsSeen(int channel) const noexcept;
    [[nodiscard]] std::uint64_t droppedSamplesTotal(int channel) const noexcept;

    /// The chain running `weighting`'s blocks that closed during the most
    /// recent `feedHop` call on `channel`, oldest first -- empty between
    /// calls, when the hop just fed did not complete one, or when this
    /// session has no chain for `weighting`. Cleared at the top of every
    /// `feedHop`, so a caller that drains this right after `feedHop` sees
    /// each block exactly once.
    ///
    /// PER CHAIN, not per channel (fix round 2026-09-25, verifier HIGH
    /// finding: every consumer previously read the FIRST configured chain
    /// regardless of which metric it was actually about -- an alarm on
    /// `LAeq` read a configured `LCeq`'s numbers whenever C happened to be
    /// listed first). Lane L6a task W2-E1: this is what `SplChannelState`
    /// (history, alarms, dose, the Ln histogram) is fed from, on the
    /// analysis thread, block by block -- never from a `Snapshot`, which is
    /// a throttled copy for the message thread and can be built less often
    /// than a block closes.
    [[nodiscard]] std::span<const rta::meter::Block> newlyClosedBlocks(
        int channel, rta::dsp::WeightingType weighting) const noexcept;

    /// The latest block on `channel`'s FIRST chain -- the one `Snapshot`'s
    /// held maxima and sampled peak are read from.
    [[nodiscard]] std::optional<rta::meter::Block> latestBlock(int channel) const noexcept;
    /// `channel`'s FIRST chain's window, oldest first and contiguous, or an
    /// empty span.
    [[nodiscard]] std::span<const rta::meter::Block> window(int channel) const noexcept;
    /// The window of the chain running `weighting`, or an EMPTY span when this
    /// session has no such chain. Empty is how a caller learns it asked for
    /// something the config never named -- never a silent fall back to a
    /// different weighting's numbers.
    [[nodiscard]] std::span<const rta::meter::Block> window(
        int channel, rta::dsp::WeightingType weighting) const noexcept;

    /// Fills `out[i]` with metric `i`'s OWN window and returns how many
    /// entries it wrote: `min(out.size(), config()->metrics.size())`.
    ///
    /// THE FIRST N, never all-or-nothing (PR #17 verifier defect 1). It used
    /// to return 0 when `out` was shorter than the metric list, and a 0 read
    /// downstream as "no per-metric windows supplied", which the publish path
    /// took as permission to fall back to one shared window -- so an
    /// undersized buffer turned into a wrong number under a right label
    /// instead of into a short answer. A partial fill plus
    /// `buildSplBlockView`'s no-fallback rule makes the same input produce
    /// ABSENCE for the rows nobody filled.
    ///
    /// This is what `buildSplBlockView` reads, and it is the whole reason the
    /// chains exist: metric `i`'s WEIGHTING decides which window it is
    /// averaged over. Allocation-free -- the caller supplies the storage.
    std::size_t fillMetricWindows(
        int channel, std::span<std::span<const rta::meter::Block>> out) const noexcept;

    /// How many distinct weightings this session's metrics named.
    [[nodiscard]] std::size_t chainCount() const noexcept { return weightings_.size(); }
    [[nodiscard]] std::span<const rta::dsp::WeightingType> weightings() const noexcept {
        return weightings_;
    }

    [[nodiscard]] const SplConfig* config() const noexcept {
        return running_ ? &config_ : nullptr;
    }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }

private:
    /// One `SplMeter` and one window, for ONE weighting.
    ///
    /// ONE CHAIN PER DISTINCT WEIGHTING, and that is not a refinement -- it is
    /// what stops `SplConfig::metrics` carrying a field the code ignores.
    /// `SplMeter` runs one weighting per instance (W0-B), so a session
    /// publishing both `LAeq` and a C-weighted level needs two chains on the
    /// same channel. An earlier revision of this file built a single
    /// A-weighted meter per channel and read EVERY metric's window from it,
    /// which would have served a C-weighted metric A-weighted numbers and said
    /// nothing about it. Two metrics naming the SAME weighting share one
    /// chain, because they differ only in window length and `combineBlocks` is
    /// a recompute over whatever tail it is handed.
    struct Chain {
        Chain(const SplConfig& config, rta::dsp::WeightingType weighting, double sampleRate,
              std::size_t windowCapacity);

        rta::dsp::WeightingType weighting;
        SplMeter meter;
        std::vector<rta::meter::Block> window;  ///< oldest first, capacity fixed at start()
        std::uint64_t blocks = 0;
        std::uint32_t flagsSeen = 0;
        std::uint64_t droppedSamplesTotal = 0;

        /// THIS chain's blocks closed during the CURRENT `feedHop` call.
        /// Reserved once, at `start()`, to
        /// `rta::meter::BlockAccumulator::kReadyCapacity` -- the most a
        /// single `push()` can ever complete (that class's own bound: `push`
        /// stops consuming once `kReadyCapacity` blocks are waiting) -- so
        /// draining it after every `feedHop` allocates nothing. PER CHAIN
        /// (fix round 2026-09-25), not per channel: each weighting closes
        /// its OWN block from the SAME hop, and a caller asking for one
        /// weighting's blocks must never see another's.
        std::vector<rta::meter::Block> newlyClosed;
    };

    struct ChannelState {
        /// One per entry of `weightings_`, in the same order. Reserved once.
        std::vector<Chain> chains;
        std::uint64_t lastBusDropCount = 0;
        bool dropBaselineSet = false;
    };

    [[nodiscard]] ChannelState* state(int channel) noexcept;
    [[nodiscard]] const ChannelState* state(int channel) const noexcept;
    [[nodiscard]] const Chain* chain(int channel, rta::dsp::WeightingType w) const noexcept;

    /// TRUNCATED at `start` to at most `SplConfig::kMaxMetrics` metrics, so
    /// every consumer downstream of this class sees a list the publish path
    /// has storage for. `config()` returns this one, not the caller's.
    SplConfig config_;
    std::size_t refusedMetrics_ = 0;
    double sampleRate_ = 0.0;
    bool running_ = false;
    std::size_t windowCapacity_ = 1;
    /// The distinct weightings `config_.metrics` named, in first-seen order.
    /// At most three: A, C and Z is the whole of `rta::dsp::WeightingType`, so
    /// a channel can never need a chain it cannot have.
    std::vector<rta::dsp::WeightingType> weightings_;
    std::array<std::unique_ptr<ChannelState>, kMaxLoggedChannels> channels_;
};

}  // namespace rta::measure
