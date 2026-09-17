// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#include "rta/meter/Block.h"

#include <algorithm>
#include <cmath>

namespace rta::meter {

namespace {

/// 10*log10(meanSquare), floored at kLevelFloorDb rather than reaching -inf,
/// exactly as Detector::levelDb does for a zero state. The offset is NOT
/// applied here: a Block stores un-offset dB so that a calibration offset
/// discovered later can be applied to the log without rewriting it.
float levelDbFloored(double meanSquare) noexcept {
    if (!(meanSquare > 0.0)) return static_cast<float>(kLevelFloorDb);
    const double db = 10.0 * std::log10(meanSquare);
    return static_cast<float>(db < kLevelFloorDb ? kLevelFloorDb : db);
}

}  // namespace

BlockAccumulator::BlockAccumulator(std::uint32_t blockSamples, double sampleRate)
    : blockSamples_(blockSamples == 0 ? 1u : blockSamples)
    , fast_(TimeWeighting::Fast, sampleRate)
    , slow_(TimeWeighting::Slow, sampleRate) {}

void BlockAccumulator::reset() noexcept {
    pending_ = 0;
    nextIndex_ = 0;
    sumSquares_ = 0.0;
    maxFastMeanSquare_ = 0.0;
    maxSlowMeanSquare_ = 0.0;
    maxPeakSquare_ = 0.0;
    droppedSamples_ = 0;
    flags_ = 0;
    fast_.reset();
    slow_.reset();
    readyHead_ = 0;
    readyCount_ = 0;
}

void BlockAccumulator::noteDroppedSamples(std::uint32_t count) noexcept {
    if (count == 0) return;
    droppedSamples_ += count;
    flags_ |= flagMask(BlockFlag::Gap);
}

void BlockAccumulator::setFlag(BlockFlag f) noexcept { flags_ |= flagMask(f); }

void BlockAccumulator::closeBlock() noexcept {
    Block b;
    b.blockIndex = nextIndex_++;
    b.blockSamples = blockSamples_;
    b.droppedSamples = droppedSamples_;
    b.sumSquares = sumSquares_;
    b.maxFastDb = levelDbFloored(maxFastMeanSquare_);
    b.maxSlowDb = levelDbFloored(maxSlowMeanSquare_);
    b.peakDb = levelDbFloored(maxPeakSquare_);
    b.flags = flags_;

    const std::size_t slot = (readyHead_ + readyCount_) % kReadyCapacity;
    ready_[slot] = b;
    ++readyCount_;

    // The detectors are NOT reset: they are a continuous time weighting over
    // the whole session, and resetting them at a block boundary would make
    // every block's first samples rise from silence. Only the per-block
    // maxima, the energy and the flags rearm.
    pending_ = 0;
    sumSquares_ = 0.0;
    maxFastMeanSquare_ = 0.0;
    maxSlowMeanSquare_ = 0.0;
    maxPeakSquare_ = 0.0;
    droppedSamples_ = 0;
    flags_ = 0;
}

std::size_t BlockAccumulator::push(std::span<const float> weighted,
                                   std::span<const float> peakStream) noexcept {
    if (weighted.size() != peakStream.size()) return 0;

    std::size_t consumed = 0;
    while (consumed < weighted.size()) {
        if (readyCount_ >= kReadyCapacity) break;

        const std::size_t room = static_cast<std::size_t>(blockSamples_ - pending_);
        const std::size_t take = std::min(room, weighted.size() - consumed);

        for (std::size_t i = 0; i < take; ++i) {
            const float x = weighted[consumed + i];
            const double xd = static_cast<double>(x);
            sumSquares_ += xd * xd;

            maxFastMeanSquare_ = std::max(maxFastMeanSquare_, fast_.processSample(x));
            maxSlowMeanSquare_ = std::max(maxSlowMeanSquare_, slow_.processSample(x));

            const float p = peakStream[consumed + i];
            const double pd = static_cast<double>(p);
            maxPeakSquare_ = std::max(maxPeakSquare_, pd * pd);
        }

        pending_ += static_cast<std::uint32_t>(take);
        consumed += take;
        if (pending_ >= blockSamples_) closeBlock();
    }
    return consumed;
}

std::optional<Block> BlockAccumulator::poll() noexcept {
    if (readyCount_ == 0) return std::nullopt;
    const Block b = ready_[readyHead_];
    readyHead_ = (readyHead_ + 1) % kReadyCapacity;
    --readyCount_;
    return b;
}

WindowResult combineBlocks(std::span<const Block> blocks, double sampleRate,
                           double referenceOffsetDb, std::uint64_t windowBlocks) noexcept {
    WindowResult r;

    double sumSquares = 0.0;
    std::uint64_t samples = 0;
    double maxFast = kLevelFloorDb;
    double maxSlow = kLevelFloorDb;
    double peak = kLevelFloorDb;

    for (const Block& b : blocks) {
        if (hasFlag(b.flags, BlockFlag::Overload)) ++r.overloadBlocks;
        if (hasFlag(b.flags, BlockFlag::UnderRange)) ++r.underRangeBlocks;
        if (hasFlag(b.flags, BlockFlag::Dropped)) ++r.droppedBlocks;
        if (hasFlag(b.flags, BlockFlag::Gap)) ++r.gapBlocks;
        r.droppedSamplesTotal += b.droppedSamples;

        if ((b.flags & kExcludingFlags) != 0u) {
            ++r.excludedBlocks;
            continue;
        }

        // Summed in the buffer's own order, so a recompute over a membership
        // that lost one block is bit-identical to a fresh computation over
        // the survivors -- which is the property a running subtraction cannot
        // provide (record §3; W0-A A5).
        sumSquares += b.sumSquares;
        samples += b.blockSamples;
        ++r.blocks;
        maxFast = std::max(maxFast, static_cast<double>(b.maxFastDb));
        maxSlow = std::max(maxSlow, static_cast<double>(b.maxSlowDb));
        peak = std::max(peak, static_cast<double>(b.peakDb));
    }

    r.samples = samples;
    r.seconds = (sampleRate > 0.0) ? static_cast<double>(samples) / sampleRate : 0.0;

    if (windowBlocks > 0) {
        const double fill = static_cast<double>(blocks.size()) / static_cast<double>(windowBlocks);
        r.bufferFill = fill > 1.0 ? 1.0 : fill;
    }

    if (samples > 0 && sumSquares > 0.0) {
        r.leqDb = 10.0 * std::log10(sumSquares / static_cast<double>(samples)) + referenceOffsetDb;
        r.maxFastDb = maxFast + referenceOffsetDb;
        r.maxSlowDb = maxSlow + referenceOffsetDb;
        r.peakDb = peak + referenceOffsetDb;
    } else if (samples > 0) {
        // Measured silence reads as the floor; "no data yet" (below) does not
        // read at all. The two are different facts, and Leq.h:66-68 already
        // draws the same line.
        r.leqDb = kLevelFloorDb + referenceOffsetDb;
        r.maxFastDb = maxFast + referenceOffsetDb;
        r.maxSlowDb = maxSlow + referenceOffsetDb;
        r.peakDb = peak + referenceOffsetDb;
    }

    return r;
}

}  // namespace rta::meter
