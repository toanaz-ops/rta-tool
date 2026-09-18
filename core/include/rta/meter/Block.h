// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/meter/Detector.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace rta::meter {

/// The §2 measurement block: a fixed number of samples of accumulated energy,
/// plus the two time-weighted maxima and the sampled peak held over it.
///
/// LAYOUT IS LOAD-BEARING. The record's field list is 36 bytes of payload and
/// 40 as laid out, because the `double` forces 4 bytes of padding after the
/// leading uint64 + uint32. `droppedSamples` occupies exactly that padding, so
/// the block gained a field and `sizeof(Block)` did not move -- record §4's
/// ring-size table is unchanged. The three static_asserts below are what make
/// that a measurement rather than an assumption; a compiler that disagrees
/// fails the build instead of silently resizing an eight-hour ring.
struct Block {
    std::uint64_t blockIndex = 0;      ///< THE CLOCK. No wall time in any arithmetic.
    std::uint32_t blockSamples = 0;    ///< the divisor every mean in this block used
    std::uint32_t droppedSamples = 0;  ///< samples the bus LOST before this block closed.
                                       ///< In the former padding, so the block is still
                                       ///< 40 B. A reader sums the column to reconstruct
                                       ///< elapsed time: Sigma(blockSamples+droppedSamples).
    double sumSquares = 0.0;           ///< the energy; leqDb = 10log10(sumSquares/blockSamples)
    float maxFastDb = static_cast<float>(kLevelFloorDb);  ///< MAX-HELD over the block, not sampled
    float maxSlowDb = static_cast<float>(kLevelFloorDb);
    float peakDb = static_cast<float>(kLevelFloorDb);  ///< C-weighted, SAMPLED peak
    std::uint32_t flags = 0;                           ///< BlockFlag bitmask
};

static_assert(sizeof(Block) == 40,
              "sizeof(Block) != 40 -- record §4's whole ring table is sized on 40 bytes");
static_assert(alignof(Block) == 8, "alignof(Block) != 8 -- the double's alignment moved");
static_assert(offsetof(Block, droppedSamples) == 12,
              "droppedSamples is no longer in the padding the double's alignment wastes");

enum class BlockFlag : std::uint32_t {
    Overload = 1u << 0,
    UnderRange = 1u << 1,
    Dropped = 1u << 2,
    CalibrationInvalid = 1u << 3,
    Gap = 1u << 4,  ///< the paired drain stalled; `droppedSamples` says by how much
};

[[nodiscard]] constexpr std::uint32_t flagMask(BlockFlag f) noexcept {
    return static_cast<std::uint32_t>(f);
}

[[nodiscard]] constexpr bool hasFlag(std::uint32_t flags, BlockFlag f) noexcept {
    return (flags & flagMask(f)) != 0u;
}

/// WHICH FLAGS EXCLUDE A BLOCK FROM combineBlocks' MEMBERSHIP -- one decision,
/// and it changes every published Leq during a loud show, so it is stated here
/// rather than guessed at each call site.
///
///   CalibrationInvalid  EXCLUDES.  Its dB are referenced to an offset ISO
///                       1996-2:2017 cl. 5.2's own discard rule says must not
///                       be trusted, and that is the ONE published normative
///                       criterion this lane has.
///   Overload            INCLUDES.  A clipped waveform carries LESS energy
///                       than the signal that clipped it, so dropping the
///                       block removes the loudest moment of the show and
///                       biases the compliance number in the operator's
///                       favour -- the same failure §2 rejects when it
///                       refuses instantaneous sampling.
///   UnderRange          INCLUDES.  Excluding floor readings biases Leq UP.
///   Dropped, Gap        INCLUDE.  The energy they carry is real over the
///                       samples that existed, and `blockSamples` is the
///                       honest divisor for it. What is lost is TIME, and
///                       that is what `droppedSamples` records.
///
/// Every one of the five is COUNTED and REPORTED either way (record §9 item 8).
/// IEC 61672-1 cl. 3.28's validity definition and ISO 1996-2 cl. 10.3 are
/// paywalled and unread, so no standard basis is claimed for the four
/// inclusions -- only for the one exclusion.
[[nodiscard]] constexpr bool excludesFromWindow(BlockFlag f) noexcept {
    return f == BlockFlag::CalibrationInvalid;
}

/// The mask of every flag that excludes -- the single place the membership
/// filter reads, so a new excluding flag is one edit and not a hunt.
inline constexpr std::uint32_t kExcludingFlags = flagMask(BlockFlag::CalibrationInvalid);

/// Accumulates one block: the sum of squares on the weighted stream, the two
/// detector maxima, and the sampled peak on the peak stream.
///
/// The detectors are NOT reset at a block boundary: they are a continuous time
/// weighting over the whole session, and rearming them per block would make
/// every block rise from silence. Only the energy, the per-block maxima, the
/// dropped-sample count and the flags rearm.
///
/// THE OVERLOAD RUN IS NOT HERE, and that is deliberate. Overload is a fact
/// about the RAW converter stream, and every sample this class sees has
/// already been through a weighting filter that changed its value -- a run
/// measured on a weighted stream is a run in a signal the hardware never
/// delivered. Measured: the C-weighted copy of three samples at
/// `rta::dsp::kFullScaleThreshold` does not reach that threshold at all. The
/// latch therefore lives one layer up, in `rta::measure::SplMeter`, where the
/// raw hop is still in hand (SPL-R4), and this class takes the resulting flag
/// through `setFlag`.
///
/// No allocation after construction. Pure numbers in, numbers out: no clock,
/// no file, no policy -- an Overload, UnderRange or CalibrationInvalid
/// criterion is a caller's decision, supplied through `setFlag`.
class BlockAccumulator {
public:
    /// How many completed blocks may wait for `poll()` before `push` stops
    /// consuming. Four is generous: a caller that polls after every push
    /// never reaches two, because a push of at most `blockSamples` samples
    /// can complete at most one block.
    static constexpr std::size_t kReadyCapacity = 4;

    /// @param blockSamples  the fixed divisor every mean in a block uses;
    ///                      must be > 0.
    /// @param sampleRate    hertz; must be > 0 (the internal Detectors throw
    ///                      otherwise).
    BlockAccumulator(std::uint32_t blockSamples, double sampleRate);

    /// Back to the state of a freshly-constructed accumulator: block index 0,
    /// no pending samples, no ready blocks, both detectors at zero, the
    /// overload run broken.
    void reset() noexcept;

    /// Consumes as much of `weighted` as it can and returns how many samples
    /// it took. Stops short only when `kReadyCapacity` blocks are already
    /// waiting -- so a caller that drains with `poll()` always sees the whole
    /// span consumed. `weighted` and `peakStream` must be the same length:
    /// the first drives the energy and both detectors, the second (the
    /// C-weighted path) drives the sampled peak and the overload run.
    ///
    /// Returns 0 if the two spans differ in length, which is a programming
    /// error this class refuses rather than guesses at.
    std::size_t push(std::span<const float> weighted, std::span<const float> peakStream) noexcept;

    /// Records that the bus LOST `count` samples immediately before whatever
    /// is pushed next. The count rides the block currently being accumulated
    /// and sets `BlockFlag::Gap` on it, so a third party holding only the log
    /// text can reconstruct the true sample position of every block.
    void noteDroppedSamples(std::uint32_t count) noexcept;

    /// Sets a policy flag on the block currently being accumulated. The
    /// criterion behind `Overload`, `UnderRange` or `CalibrationInvalid` is
    /// the caller's; core holds no threshold for any of them.
    void setFlag(BlockFlag f) noexcept;

    /// The oldest completed block, removing it, or nullopt.
    [[nodiscard]] std::optional<Block> poll() noexcept;

    [[nodiscard]] std::uint32_t blockSamples() const noexcept { return blockSamples_; }
    [[nodiscard]] std::uint32_t pendingSamples() const noexcept { return pending_; }
    [[nodiscard]] std::uint64_t nextBlockIndex() const noexcept { return nextIndex_; }
    [[nodiscard]] std::size_t readyBlocks() const noexcept { return readyCount_; }

private:
    void closeBlock() noexcept;

    std::uint32_t blockSamples_;
    std::uint32_t pending_ = 0;
    std::uint64_t nextIndex_ = 0;

    double sumSquares_ = 0.0;
    double maxFastMeanSquare_ = 0.0;
    double maxSlowMeanSquare_ = 0.0;
    double maxPeakSquare_ = 0.0;
    std::uint32_t droppedSamples_ = 0;
    std::uint32_t flags_ = 0;

    Detector fast_;
    Detector slow_;

    std::array<Block, kReadyCapacity> ready_{};
    std::size_t readyHead_ = 0;
    std::size_t readyCount_ = 0;
};

/// The result of §3's windowed recompute. Every flag is counted whether it
/// excludes or not -- record §9 item 8 prints all five.
struct WindowResult {
    /// ABSENT when membership is empty, never `kLevelFloorDb`: a live Leq
    /// shown without saying that its window is not yet full is a number that
    /// is quietly wrong (record §9).
    std::optional<double> leqDb;
    double maxFastDb = kLevelFloorDb;  ///< max over membership, + referenceOffsetDb
    double maxSlowDb = kLevelFloorDb;
    double peakDb = kLevelFloorDb;
    std::uint64_t blocks = 0;   ///< blocks IN membership
    std::uint64_t samples = 0;  ///< the divisor: sum of blockSamples over membership
    double seconds = 0.0;       ///< samples / sampleRate. Measured time, not elapsed time
    /// Blocks present in the buffer as a fraction of the window asked for,
    /// clamped to 1. Counts every block present, including excluded ones:
    /// what it answers is "has the window filled yet", not "how much energy".
    double bufferFill = 0.0;
    std::uint64_t excludedBlocks = 0;
    std::uint64_t overloadBlocks = 0;
    std::uint64_t underRangeBlocks = 0;
    std::uint64_t droppedBlocks = 0;
    std::uint64_t gapBlocks = 0;
    /// Summed over the WHOLE buffer, including excluded blocks, because what
    /// it measures is elapsed time and not energy.
    std::uint64_t droppedSamplesTotal = 0;
};

/// §3, exact: Leq(W) = 10log10( Sigma sumSquares_i / Sigma blockSamples_i ) + offset.
///
/// RECOMPUTED over the CURRENT membership on every call -- never a running
/// subtraction, because membership is mutable: a block can be retired by a
/// later calibration check, and a subtraction cannot take back a number it
/// has already folded in (record §3).
[[nodiscard]] WindowResult combineBlocks(std::span<const Block> blocks, double sampleRate,
                                         double referenceOffsetDb,
                                         std::uint64_t windowBlocks) noexcept;

/// §8, one line, one identity: the offset that makes a measured level read as
/// the calibrator's stated level.
[[nodiscard]] constexpr double calibrationOffsetDb(double calibratorLevelDb,
                                                   double measuredLevelDb) noexcept {
    return calibratorLevelDb - measuredLevelDb;
}

}  // namespace rta::meter
