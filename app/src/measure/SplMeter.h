// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Lane L6a task W0-B (record docs/dsp/2026-09-16-spl-pro-l6a.md §2, §11).
#pragma once

#include "measure/SplConfig.h"

#include "rta/dsp/Weighting.h"
#include "rta/meter/Block.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace rta::measure {

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
};

}  // namespace rta::measure
