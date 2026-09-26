// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L6a task W0-B (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md; record
// docs/dsp/2026-09-16-spl-pro-l6a.md §2, §11): the per-channel SPL chain's
// BLOCK-FLOW/ALLOCATION half -- B3 (the overload run across a block
// boundary), B4 (no allocation), and the two sample-accounting invariants
// (every pushed sample lands somewhere; an evicted block's own droppedSamples
// rides forward). Split out of test_spl_meter.cpp (LOW follow-up batch, item
// 4) when that file crossed the 400-line hard cap -- the WEIGHTING/DETECTOR
// half (B1, B2, B2b, B5, histogramBaseDb) stayed there; this file's own
// subject is what happens to a BLOCK once its samples are in, not the
// weighting curve or detector step response that produced its numbers.
//
// JUCE-free, so it is proven with RTA_BUILD_APP=OFF on all three CI operating
// systems. Every acceptance is a closed-form identity or a shipped constant
// read back, never a value this code printed.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "AllocationProbe.h"

#include "measure/SplConfig.h"
#include "measure/SplMeter.h"

#include "rta/dsp/OverloadDetector.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <vector>

using rta::dsp::WeightingType;
using rta::measure::SplConfig;
using rta::measure::SplMeter;
using rta::meter::Block;
using rta::meter::BlockFlag;

namespace {

constexpr double kFs = 48000.0;

SplConfig oneSecondConfig(double offsetDb = 0.0) {
    SplConfig config;
    config.blockSeconds = 1.0;
    config.referenceOffsetDb = offsetDb;
    return config;
}

/// Pushes `samples` through the meter in hops of `hop`, collecting every block
/// the meter completes.
std::vector<Block> feed(SplMeter& meter, std::span<const float> samples, std::size_t hop) {
    std::vector<Block> out;
    std::size_t off = 0;
    while (off < samples.size()) {
        const std::size_t n = std::min(hop, samples.size() - off);
        meter.push(samples.subspan(off, n));
        while (auto b = meter.poll()) out.push_back(*b);
        off += n;
    }
    return out;
}

}  // namespace

// --- B3: the overload run crosses the block boundary --------------------

TEST_CASE("B3 an overload run that straddles a block boundary is caught", "[splmeter]") {
    // OverloadDetector.h:22-27 warns in its own words that a run does not
    // carry across separate calls, so a hop boundary splits it in two. A BLOCK
    // boundary is a bigger version of the same boundary (SPL-R4).
    const std::uint32_t blockSamples = 480;
    SplConfig config = oneSecondConfig();
    config.blockSeconds = static_cast<double>(blockSamples) / kFs;
    SplMeter meter(config, WeightingType::Z, kFs);

    // Two full-scale samples at the END of block 0, one at the START of
    // block 1: the run of three COMPLETES in block 1, and neither block sees
    // three consecutive samples on its own.
    std::vector<float> x(2 * blockSamples, 0.01f);
    x[blockSamples - 2] = rta::dsp::kFullScaleThreshold;
    x[blockSamples - 1] = rta::dsp::kFullScaleThreshold;
    x[blockSamples] = rta::dsp::kFullScaleThreshold;

    // Hops of 160 so the run also straddles a HOP boundary, which is the
    // failure the header names.
    const auto blocks = feed(meter, x, 160);
    REQUIRE(blocks.size() == 2);
    CHECK_FALSE(rta::meter::hasFlag(blocks[0].flags, BlockFlag::Overload));
    CHECK(rta::meter::hasFlag(blocks[1].flags, BlockFlag::Overload));
}

// --- B4: no allocation after construction -------------------------------

TEST_CASE("B4 push allocates nothing after construction", "[splmeter]") {
    const std::uint32_t blockSamples = 480;
    SplConfig config = oneSecondConfig();
    config.blockSeconds = static_cast<double>(blockSamples) / kFs;
    SplMeter meter(config, WeightingType::A, kFs);

    std::vector<float> hop(160, 0.1f);
    // Ten blocks' worth of hops. Through W0-B0's SHARED probe -- a second
    // global operator new in this file would not link.
    std::size_t bytes = 0;
    {
        const rta::test::AllocationProbe probe;
        for (int i = 0; i < 10 * 3; ++i) {
            meter.push(hop);
            while (meter.poll()) {
            }
        }
        bytes = probe.bytes();
    }
    INFO("bytes allocated by 30 pushes = " << bytes);
    CHECK(bytes == 0);
}

// --- step 2 (PR #29 round-3 fix pass, MEDIUM): every pushed sample is ------
// accounted for, exactly, never silently lost -------------------------------

TEST_CASE("every sample pushed is accounted for: closed blocks + dropped + pending, exactly",
         "[splmeter]") {
    // Latent, pre-existing defect: a hop spanning several kScratchSamples
    // (1024) segments could complete more than
    // rta::meter::BlockAccumulator::kReadyCapacity (4) blocks before the
    // OLD code ever called accumulator_.poll() (only done AFTER push(hop)
    // returned) -- so the accumulator's own cap silently stopped consuming
    // partway through the hop and the rest was never processed at all, not
    // even counted as Dropped. Verifier repro: blockSeconds = 0.002, only 8
    // of 21 expected blocks, >= 1089 of 2048 pushed samples unaccounted
    // for. Record §15 A1's own invariant:
    // Sigma(blockSamples + droppedSamples) == total samples pushed.
    //
    // Constructed directly against SplMeter -- bypassing
    // SplSession::start()'s own advisory `blockSecondsBelowRecommendedFloor`
    // gate (PR #29 step 2's separate, purely advisory floor) -- so this
    // proves the FIX itself (SplMeter's own internal ready buffer, drained
    // inside the segment loop) holds for the exact configurations that
    // broke it, independent of that advisory check.
    for (const double blockSeconds : {0.002, 0.004, 0.005}) {
        INFO("blockSeconds = " << blockSeconds);
        SplConfig config = oneSecondConfig();
        config.blockSeconds = blockSeconds;
        SplMeter meter(config, WeightingType::A, kFs);

        constexpr std::size_t kHopSamples = 1024;
        constexpr int kHops = 50;
        std::vector<float> hop(kHopSamples);
        for (std::size_t n = 0; n < kHopSamples; ++n) {
            hop[n] = static_cast<float>(
                0.3 * std::sin(2.0 * std::numbers::pi * 200.0 * static_cast<double>(n) / kFs));
        }

        std::uint64_t accountedSamples = 0;
        for (int h = 0; h < kHops; ++h) {
            meter.push(hop);
            while (auto b = meter.poll()) accountedSamples += b->blockSamples + b->droppedSamples;
        }
        // The currently-open (never-yet-closed) block's own partial count --
        // the ONE place a sample can legitimately be "not yet in a Block"
        // without being lost.
        accountedSamples += meter.pendingSamples();

        const auto totalPushed = static_cast<std::uint64_t>(kHopSamples) * static_cast<std::uint64_t>(kHops);
        CHECK(accountedSamples == totalPushed);
    }
}

// --- round-4 item 2: an evicted block's OWN prior droppedSamples must ------
// ride forward, not just its blockSamples -----------------------------------

TEST_CASE("an evicted block's own droppedSamples rides forward with its blockSamples",
         "[splmeter]") {
    // PROBE4. blockSamples = 1 (the smallest legal block) and the ready
    // buffer (SplMeter::kScratchSamples slots -- SplMeter.h's own overflow
    // comment) filled to EXACT capacity, so the next block to close is
    // evicted via SplMeter.cpp's overflow-eviction fallback. That evicted
    // block ("A") already carries its OWN prior droppedSamples (a bus-drop
    // noted while it was still pending) BEFORE eviction folds its lost time
    // onto the block that inherits it ("B") -- the defect this proves is
    // that the fold used to carry only A's blockSamples and silently drop
    // A's own droppedSamples on the floor.
    SplConfig config;
    config.blockSeconds = 1e-6;  // blockSamplesFor rounds this down to 1.
    SplMeter meter(config, WeightingType::A, kFs);
    REQUIRE(meter.blockSamples() == 1);

    const std::vector<float> one(1, 0.1f);
    const std::vector<float> full(SplMeter::kScratchSamples, 0.1f);

    // Fill the ready buffer to EXACT capacity: kScratchSamples one-sample
    // blocks (indices 0..kScratchSamples-1), none evicted, none dropped.
    meter.push(full);

    // 7 bus-dropped samples, noted on the block about to close next ("A",
    // index kScratchSamples) -- BEFORE it closes, so A carries its own
    // droppedSamples = 7 the moment it is evicted.
    meter.noteDroppedSamples(7);

    // Close A. The buffer is still exactly full, so A is evicted rather than
    // stored: A's blockSamples (1) PLUS A's own droppedSamples (7) must ride
    // forward onto B, the block now pending.
    meter.push(one);

    // Drain exactly one slot so B, once closed, is STORED rather than
    // evicted in turn -- the oldest entry, a clean block from the first
    // batch, checked here as a sanity bound on the fixture itself.
    std::uint64_t accounted = 0;
    {
        const auto oldest = meter.poll();
        REQUIRE(oldest.has_value());
        CHECK(oldest->blockSamples == 1);
        CHECK(oldest->droppedSamples == 0);
        accounted += oldest->blockSamples + oldest->droppedSamples;
    }

    // Close B and read every remaining block back, oldest first; the LAST one
    // drained is B, because the ready buffer is FIFO and B was appended last.
    meter.push(one);
    std::optional<Block> blockB;
    while (auto b = meter.poll()) {
        accounted += b->blockSamples + b->droppedSamples;
        blockB = b;
    }
    REQUIRE(blockB.has_value());

    INFO("block B droppedSamples = " << blockB->droppedSamples);
    // 1 (A's OWN blockSamples) + 7 (A's OWN prior droppedSamples) = 8,
    // exactly -- an integer identity, not a DSP tolerance. The pre-fix code
    // read 1 here (A's own droppedSamples silently dropped on the floor).
    CHECK(blockB->droppedSamples == 8);
    CHECK(rta::meter::hasFlag(blockB->flags, BlockFlag::Dropped));

    // Sigma(blockSamples + droppedSamples), exactly: kScratchSamples + 1
    // physical samples pushed (the full batch, A's sample and B's sample)
    // plus the 7 samples noted lost upstream of push() entirely.
    const auto totalElapsed =
        static_cast<std::uint64_t>(SplMeter::kScratchSamples) + 1 + 1 + 7;
    INFO("accounted = " << accounted << ", total elapsed = " << totalElapsed);
    CHECK(accounted == totalElapsed);
}
