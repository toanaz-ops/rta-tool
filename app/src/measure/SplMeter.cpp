// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
#include "measure/SplMeter.h"

#include "rta/dsp/OverloadDetector.h"

#include <algorithm>
#include <cmath>

namespace rta::measure {

namespace {

/// `blockSeconds` at `sampleRate`, as the sample count every mean in a block
/// divides by. Rounded rather than truncated so a rate that does not divide
/// evenly lands on the nearest whole sample instead of systematically short,
/// and floored at 1 so a nonsense configuration cannot produce a zero divisor.
std::uint32_t blockSamplesFor(double blockSeconds, double sampleRate) noexcept {
    if (!(blockSeconds > 0.0) || !(sampleRate > 0.0)) return 1;
    const double samples = blockSeconds * sampleRate + 0.5;
    if (samples < 1.0) return 1;
    return static_cast<std::uint32_t>(samples);
}

/// Record §5's 100 ms tick, rounded like `blockSamplesFor` above and floored
/// at 1 so a nonsense sample rate cannot produce a zero-sample tick period.
std::uint64_t lnSamplesPerTickFor(double sampleRate) noexcept {
    if (!(sampleRate > 0.0)) return 1;
    const double samples = 0.1 * sampleRate + 0.5;
    if (samples < 1.0) return 1;
    return static_cast<std::uint64_t>(samples);
}

/// `max(1, round(blockSeconds / 0.1))` -- how many 100 ms ticks one block
/// spans, the unit `lnTickBufferCapacity` below is generous against.
std::uint64_t ticksPerBlockFor(double blockSeconds) noexcept {
    if (!(blockSeconds > 0.0)) return 1;
    const double ticks = blockSeconds / 0.1 + 0.5;
    if (ticks < 1.0) return 1;
    return static_cast<std::uint64_t>(ticks);
}

/// `(kReadyCapacity + 2) * ticksPerBlock`, floored at 16 -- mirrors
/// `SplSession::Chain::newlyClosed`'s own sizing (that class's own header
/// comment): generous against how many ticks a single `push()` call (at
/// most one hop) can produce before the caller drains it.
std::size_t lnTickBufferCapacity(double blockSeconds) noexcept {
    const std::uint64_t ticksPerBlock = ticksPerBlockFor(blockSeconds);
    const std::uint64_t generous =
        (rta::meter::BlockAccumulator::kReadyCapacity + 2) * ticksPerBlock;
    return static_cast<std::size_t>(generous < 16 ? 16 : generous);
}

}  // namespace

SplMeter::SplMeter(const SplConfig& config, rta::dsp::WeightingType weighting, double sampleRate)
    : weightingType_(weighting)
    , sampleRate_(sampleRate)
    , referenceOffsetDb_(config.referenceOffsetDb)
    , mainWeighting_(weighting, sampleRate)
    , peakWeighting_(rta::dsp::WeightingType::C, sampleRate)
    , accumulator_(blockSamplesFor(config.blockSeconds, sampleRate), sampleRate)
    , lnDetector_(rta::meter::TimeWeighting::Fast, sampleRate)
    , lnSamplesPerTick_(lnSamplesPerTickFor(sampleRate)) {
    lnTicks_.reserve(lnTickBufferCapacity(config.blockSeconds));
}

void SplMeter::reset() noexcept {
    mainWeighting_.reset();
    peakWeighting_.reset();
    accumulator_.reset();
    overloadRun_ = 0;
    lnDetector_.reset();
    lnSampleCounter_ = 0;
    lnTicks_.clear();
    overflowedLnTicks_ = 0;
    readyHead_ = 0;
    readyCount_ = 0;
}

void SplMeter::noteDroppedSamples(std::uint32_t count) noexcept {
    accumulator_.noteDroppedSamples(count);
}

void SplMeter::setFlag(rta::meter::BlockFlag flag) noexcept { accumulator_.setFlag(flag); }

std::optional<rta::meter::Block> SplMeter::poll() noexcept {
    if (readyCount_ > 0) {
        const rta::meter::Block b = readyBuffer_[readyHead_];
        readyHead_ = (readyHead_ + 1) % kReadyBufferCapacity;
        --readyCount_;
        return b;
    }
    // Should be empty by construction once push() has run (see
    // readyBuffer_'s own comment) -- the fallthrough costs nothing and
    // keeps this the one seam a caller ever needs to drain.
    return accumulator_.poll();
}

void SplMeter::push(std::span<const float> hop) noexcept {
    // Cleared at the TOP, so every caller of push() gets a fresh view of
    // exactly what THIS call ticked -- mirrors SplSession::feedHop already
    // clearing c.newlyClosed before c.meter.push(hop), moved one layer in.
    lnTicks_.clear();

    std::size_t off = 0;
    while (off < hop.size()) {
        // Two bounds at once: the fixed scratch, and the samples left in the
        // block currently being accumulated. Splitting at the BLOCK boundary
        // is what makes the overload flag land on the right block -- every
        // sample in `segment` belongs to one block by construction.
        const std::size_t room =
            static_cast<std::size_t>(accumulator_.blockSamples() - accumulator_.pendingSamples());
        const std::size_t n = std::min(std::min(kScratchSamples, room), hop.size() - off);
        const auto segment = hop.subspan(off, n);

        const std::span<float> main(mainScratch_.data(), n);
        const std::span<float> peak(peakScratch_.data(), n);
        // The SHIPPED weighting cascade, not a curve reimplemented here. The
        // filter state carries across segments, so splitting a hop changes
        // nothing about the result. Z holds zero sections and
        // BiquadCascade::process copies input to output bit-identically, so
        // no special case is needed for it.
        mainWeighting_.process(segment, main);
        peakWeighting_.process(segment, peak);

        // RECORD §5's Ln FEED: the SAME weighted `main` span, one sample at
        // a time, into a SECOND Fast detector sampled on its own persistent
        // 100 ms clock -- independent of block closure (ticks are NOT tied
        // to `blockSamples`/`pending_`; the counter here never resets at a
        // block boundary). `lnSampleCounter_` is the running TOTAL across
        // every push() this meter has ever seen, so a tick lands on the
        // correct absolute 100 ms boundary regardless of how a hop happens
        // to be sliced into segments.
        for (std::size_t i = 0; i < n; ++i) {
            lnDetector_.processSample(main[i]);
            ++lnSampleCounter_;
            if (lnSampleCounter_ % lnSamplesPerTick_ == 0) {
                if (lnTicks_.size() < lnTicks_.capacity()) {
                    lnTicks_.push_back(lnDetector_.levelDb());
                } else {
                    ++overflowedLnTicks_;
                }
            }
        }

        // THE OVERLOAD RUN, on the RAW segment, BEFORE the block is fed.
        //
        // Raw and not weighted, because overload is a fact about what the
        // converter delivered: every sample that reaches the accumulator has
        // been through a filter that changed its value, and measured, the
        // C-weighted copy of three samples at kFullScaleThreshold does not
        // reach that threshold at all -- a run detected there would be a run
        // in a signal the hardware never produced.
        //
        // `rta::dsp::hasOverload` cannot serve here either: its own header
        // (OverloadDetector.h:22-27) says a run does not carry across separate
        // calls, so a hop boundary splits one in two, and a block boundary is
        // a bigger version of the same boundary (SPL-R4). `overloadRun_` is a
        // member, so a run survives a segment, a hop AND a block boundary, and
        // the flag lands on the block the run COMPLETES in.
        //
        // The threshold and the three-consecutive-sample criterion stay
        // Smaart LE v9.1 p.78's own published numbers, read from rta::dsp and
        // never restated here.
        for (std::size_t i = 0; i < n; ++i) {
            if (std::fabs(segment[i]) >= rta::dsp::kFullScaleThreshold) {
                ++overloadRun_;
                if (overloadRun_ >= kOverloadRunLength) {
                    accumulator_.setFlag(rta::meter::BlockFlag::Overload);
                }
            } else {
                overloadRun_ = 0;
            }
        }

        const std::size_t consumed = accumulator_.push(main, peak);

        // DRAIN IMMEDIATELY (fix round 2026-09-25 step 2), into THIS
        // class's own ready buffer -- see readyBuffer_'s own comment for
        // why: this is what keeps accumulator_'s kReadyCapacity(4) from
        // ever being reached within one push(hop) call, for a hop spanning
        // any number of kScratchSamples-sized segments.
        while (auto block = accumulator_.poll()) {
            if (readyCount_ < kReadyBufferCapacity) {
                readyBuffer_[(readyHead_ + readyCount_) % kReadyBufferCapacity] = *block;
                ++readyCount_;
            } else {
                // readyBuffer_ itself is exhausted -- see its own comment
                // for how far out of realistic range that is. Counted, not
                // silently discarded: the already-closed block's own sample
                // count rides whatever block is CURRENTLY pending, exactly
                // the same "flag and count ride the block being
                // accumulated" convention noteDroppedSamples/setFlag
                // already document.
                accumulator_.setFlag(rta::meter::BlockFlag::Dropped);
                accumulator_.noteDroppedSamples(block->blockSamples);
            }
        }

        if (consumed < n) {
            // Unreachable in ordinary operation now that the accumulator is
            // drained every segment (its own readyCount_ never reaches
            // kReadyCapacity within this loop) -- kept as the same
            // counted-loss fallback it always was, in case a future change
            // to the drain above ever lets it happen again.
            accumulator_.setFlag(rta::meter::BlockFlag::Dropped);
            accumulator_.noteDroppedSamples(static_cast<std::uint32_t>(n - consumed));
            return;
        }
        off += n;
    }
}

double SplMeter::blockLevelDb(const rta::meter::Block& block, double referenceOffsetDb) noexcept {
    if (block.blockSamples == 0 || !(block.sumSquares > 0.0)) {
        return rta::meter::kLevelFloorDb + referenceOffsetDb;
    }
    return 10.0 * std::log10(block.sumSquares / static_cast<double>(block.blockSamples))
           + referenceOffsetDb;
}

}  // namespace rta::measure
