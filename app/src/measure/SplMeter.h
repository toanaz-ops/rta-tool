// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Lane L6a task W0-B (record docs/dsp/2026-09-16-spl-pro-l6a.md §2, §11).
#pragma once

#include "measure/SplConfig.h"

#include "rta/dsp/Weighting.h"
#include "rta/meter/Block.h"
#include "rta/meter/Detector.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rta::measure {

/// `blockSeconds` at `sampleRate`, as the sample count every mean in a block
/// divides by. Rounded rather than truncated so a rate that does not divide
/// evenly lands on the nearest whole sample instead of systematically short,
/// and floored at 1 so a nonsense configuration cannot produce a zero divisor.
///
/// EXPORTED (not file-local) since W2-E2a: `SplLogPipeline` stamps this same
/// figure into a log's header before the first block closes, and computing it
/// a second, independent way would risk a one-sample disagreement with this
/// class's own `accumulator_` at a rate that does not divide evenly.
[[nodiscard]] std::uint32_t blockSamplesFor(double blockSeconds, double sampleRate) noexcept;

/// The per-channel SPL chain for ONE frequency weighting:
///
///     hop -> Weighting(W) -> BlockAccumulator { energy, Fast, Slow }
///         -> Weighting(C) -> the sampled C-weighted peak
///         -> RAW           -> the overload run
///
/// Fed HOPS the analysis thread has already drained (record §0 hazard 2: the
/// measurement ring has one read cursor and `drainPaired` owns it, so this is
/// a tap on the scratch buffers that drain already filled, never a second
/// reader). It emits `rta::meter::Block`s on a SAMPLE-COUNT clock -- no wall
/// time is an input to any mean (record §2).
///
/// ONE WEIGHTING PER INSTANCE, deliberately. A session publishing both an
/// A-weighted and a C-weighted broadband level builds two of these on the same
/// channel; `SplConfig::metrics` is what says how many. That keeps `poll()`
/// returning a single `Block` -- the thing the log writes a row of -- rather
/// than a container it would have to allocate.
///
/// NO ALLOCATION AFTER CONSTRUCTION. The weighting scratch is a fixed array
/// and a hop of any length is processed in chunks of it; nothing here resizes.
/// This is the class `AnalysisThread` calls from inside a drain, so an
/// allocation would be a pause during a show (W0-B4 measures it at 0 bytes).
class SplMeter {
public:
    /// How many samples the internal weighting scratch holds. A hop longer
    /// than this is processed in several passes; the filter state carries
    /// across them, so the result is identical to one pass.
    static constexpr std::size_t kScratchSamples = 1024;

    /// Smaart LE v9.1 p.78's own published criterion (record §8.1): three or
    /// more CONSECUTIVE samples at or above `rta::dsp::kFullScaleThreshold`.
    /// The default of `rta::dsp::hasOverload`, named here because that
    /// function cannot be the one used (see `overloadRun_` below) and a
    /// criterion nobody can point at is a criterion nobody can check.
    static constexpr int kOverloadRunLength = 3;

    /// @param config     read once, here. `blockSeconds` and
    ///                   `referenceOffsetDb` are fixed for the session
    ///                   (record §10).
    /// @param weighting  the frequency weighting the energy and BOTH
    ///                   detectors see.
    /// @param sampleRate hertz; must be > 0.
    SplMeter(const SplConfig& config, rta::dsp::WeightingType weighting, double sampleRate);

    /// Back to a freshly-constructed state: block index 0, both filters and
    /// both detectors cleared, the overload run broken.
    void reset() noexcept;

    /// Feeds one hop. Every sample is consumed; completed blocks wait for
    /// `poll()`. Allocation-free.
    void push(std::span<const float> hop) noexcept;

    /// Records that the bus LOST `count` samples immediately before the next
    /// push. Sets `BlockFlag::Gap` on the block being accumulated and rides
    /// the count on it, so a reader holding only the log text can reconstruct
    /// the true sample position (SPL-R1, SPL-R2).
    void noteDroppedSamples(std::uint32_t count) noexcept;

    /// Sets a policy flag on the block being accumulated -- `UnderRange` or
    /// `CalibrationInvalid`, whose criteria are the app's and not core's.
    void setFlag(rta::meter::BlockFlag flag) noexcept;

    /// The oldest completed block, removing it, or nullopt.
    [[nodiscard]] std::optional<rta::meter::Block> poll() noexcept;

    [[nodiscard]] rta::dsp::WeightingType weighting() const noexcept { return weightingType_; }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] double referenceOffsetDb() const noexcept { return referenceOffsetDb_; }
    [[nodiscard]] std::uint32_t blockSamples() const noexcept {
        return accumulator_.blockSamples();
    }
    [[nodiscard]] std::uint32_t pendingSamples() const noexcept {
        return accumulator_.pendingSamples();
    }

    /// The published broadband level for a completed block: its own energy,
    /// mean-square referenced, plus the calibration offset.
    ///
    /// MEAN-SQUARE REFERENCED, which is record §13 Q1's own proposal and the
    /// convention `rta::meter::Leq` already implements (IEC 61672-1 cl. 3.9).
    /// `bands` / `spectrumDb` stay SINE-referenced dBFS (`Levels.h:22`). The
    /// 3.0103 dB between the two conventions is absorbed by
    /// `referenceOffsetDb` at this one seam and nowhere else -- W0-E is the
    /// closed-form test that a full-scale sine reads the same dB through both
    /// paths once it is applied.
    [[nodiscard]] static double blockLevelDb(const rta::meter::Block& block,
                                             double referenceOffsetDb) noexcept;

    /// Record §5: Ln is fed "the time-weighted level... at the detector
    /// sampling rate", Fast, 100 ms -- NEVER `Block::maxFastDb`, which is a
    /// max-HELD-per-BLOCK quantity and reads the loudest moment of an entire
    /// block regardless of how quiet the rest of it was (fix round
    /// 2026-09-25). This is a SECOND `rta::meter::Detector` (Fast), fed the
    /// SAME weighted `main` scratch span `push()` already computes for the
    /// accumulator -- a deliberate duplicate of the Fast detector state
    /// already running inside `BlockAccumulator` (which is private): feeding
    /// it the identical sample sequence in the identical order makes it
    /// bit-identical, and this keeps the sampling concern out of core's
    /// generic `BlockAccumulator`.
    ///
    /// Ticks recorded since the START of the MOST RECENT `push()` call --
    /// cleared at the TOP of `push()` (mirrors `SplSession::feedHop` already
    /// clearing `c.newlyClosed` before `c.meter.push(hop)`, except the clear
    /// happens INSIDE this class so every caller of `push()` gets a fresh
    /// view automatically). NO offset applied -- the same convention
    /// `Block::maxFastDb` already used; the caller adds `referenceOffsetDb`,
    /// exactly like the old `aChain->block.maxFastDb + referenceOffsetDb_`
    /// line did.
    [[nodiscard]] std::span<const double> newlyTickedLnLevelsDb() const noexcept {
        return lnTicks_;
    }

    /// How many 100 ms ticks this meter had to drop because the fixed tick
    /// buffer (sized from `SplConfig::blockSeconds` at construction) filled
    /// during one `push()` call -- COUNTED, never silent, mirroring
    /// `SplHistory::kMaxMarkers`/`overflowedMarkers()`'s own pattern.
    [[nodiscard]] std::uint64_t overflowedLnTicks() const noexcept { return overflowedLnTicks_; }

private:
    rta::dsp::WeightingType weightingType_;
    double sampleRate_;
    double referenceOffsetDb_;

    rta::dsp::Weighting mainWeighting_;
    /// The peak and overload path is C-weighted (record §2: `peakDb` is
    /// "C-weighted, SAMPLED"). A separate instance even when the main
    /// weighting is also C: two filters over the same stream need two states.
    rta::dsp::Weighting peakWeighting_;

    rta::meter::BlockAccumulator accumulator_;

    /// The consecutive-full-scale-sample run, carried across `push()` calls
    /// AND across block boundaries (SPL-R4), read off the RAW hop.
    ///
    /// RAW, not weighted, and that is the whole point: overload is a fact
    /// about what the converter delivered. Both the A and the C cascade change
    /// a full-scale sample's value, so a run measured on either stream is a
    /// run in a signal the hardware never saw -- measured here, the
    /// C-weighted copy of three samples at `kFullScaleThreshold` does not
    /// reach the threshold at all and the block reads clean.
    ///
    /// `rta::dsp::hasOverload` stays the per-hop fast path for callers that
    /// want one; it cannot be used here because its own header says a run does
    /// not carry across separate calls.
    int overloadRun_ = 0;

    std::array<float, kScratchSamples> mainScratch_{};
    std::array<float, kScratchSamples> peakScratch_{};

    /// Record §5's Ln feed: a second Fast detector over the SAME weighted
    /// stream the accumulator sees, sampled every 100 ms on a persistent
    /// (never block-reset) sample counter.
    rta::meter::Detector lnDetector_;
    std::uint64_t lnSampleCounter_ = 0;
    std::uint64_t lnSamplesPerTick_;

    /// Fixed at construction from `config.blockSeconds`: `ticksPerBlock =
    /// max(1, round(blockSeconds / 0.1))`; capacity =
    /// `max(16, (kReadyCapacity + 2) * ticksPerBlock)` -- generous against
    /// how many ticks one `push()` call (at most one hop) can produce,
    /// mirroring the sizing logic `SplSession::Chain::newlyClosed` already
    /// uses for its own per-hop buffer. Reserved once, here; `push()` never
    /// grows it -- a `push()` that would tick past capacity COUNTS the
    /// overflow (`overflowedLnTicks_`) and drops the tick rather than
    /// allocating or blocking.
    std::vector<double> lnTicks_;
    std::uint64_t overflowedLnTicks_ = 0;

    // --- fix round 2026-09-25 step 2: the sample-loss fix ------------------
    //
    // `rta::meter::BlockAccumulator::kReadyCapacity` (4) bounds how many
    // completed blocks the ACCUMULATOR holds BETWEEN drains, not how many
    // can complete within one `push(hop)` call: a hop spanning several
    // `kScratchSamples`-sized segments can complete many more than 4 blocks
    // before this class ever calls `accumulator_.poll()`, and the old code
    // only polled AFTER `push()` returned -- so the accumulator's own cap
    // silently stopped consuming partway through a hop and the remainder
    // was counted as `Dropped`, losing REAL, MEASURED samples rather than
    // excluding them by policy (violates record §15 A1's
    // Sigma(blockSamples+droppedSamples) == total-pushed invariant).
    //
    // Fix: `push()` now polls `accumulator_` INSIDE the segment loop,
    // immediately after every `accumulator_.push()` call, into THIS
    // buffer -- so the accumulator's own `readyCount_` is drained back to 0
    // before the next segment and its cap is never reached within one
    // push() call. `poll()` drains from this buffer first; a caller sees
    // no difference except that far fewer samples are ever lost.
    //
    // SIZED AT kScratchSamples (1024) blocks: each segment is capped at
    // kScratchSamples samples AND completes AT MOST ONE block (`room`,
    // below, bounds a segment to the space remaining in the block currently
    // being filled) -- 1024 is therefore the pigeonhole-safe bound for any
    // blockSamples >= 1 within a single segment. A hop far longer than
    // kScratchSamples combined with a pathologically small blockSamples
    // could in principle still exceed it; that residual case falls back to
    // the SAME counted-`Dropped` behaviour this class always had, moved
    // from a threshold of 4 to a threshold of 1024 -- more than 40x
    // headroom over the verifier's own repro (blockSeconds = 0.002, an
    // 2048-sample hop, 8 of 21 blocks lost).
    static constexpr std::size_t kReadyBufferCapacity = kScratchSamples;
    std::array<rta::meter::Block, kReadyBufferCapacity> readyBuffer_{};
    std::size_t readyHead_ = 0;
    std::size_t readyCount_ = 0;
};

}  // namespace rta::measure
