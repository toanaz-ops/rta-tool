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

}  // namespace

SplMeter::SplMeter(const SplConfig& config, rta::dsp::WeightingType weighting, double sampleRate)
    : weightingType_(weighting)
    , sampleRate_(sampleRate)
    , referenceOffsetDb_(config.referenceOffsetDb)
    , mainWeighting_(weighting, sampleRate)
    , peakWeighting_(rta::dsp::WeightingType::C, sampleRate)
    , accumulator_(blockSamplesFor(config.blockSeconds, sampleRate), sampleRate) {}

void SplMeter::reset() noexcept {
    mainWeighting_.reset();
    peakWeighting_.reset();
    accumulator_.reset();
    overloadRun_ = 0;
}

void SplMeter::noteDroppedSamples(std::uint32_t count) noexcept {
    accumulator_.noteDroppedSamples(count);
}

void SplMeter::setFlag(rta::meter::BlockFlag flag) noexcept { accumulator_.setFlag(flag); }

std::optional<rta::meter::Block> SplMeter::poll() noexcept { return accumulator_.poll(); }

void SplMeter::push(std::span<const float> hop) noexcept {
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
        if (consumed < n) {
            // BlockAccumulator only stops short when kReadyCapacity blocks are
            // already waiting, which means the caller is not polling. Record
            // the loss rather than hiding it: the flag and the count both ride
            // the block, so the log says how many samples went missing.
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
