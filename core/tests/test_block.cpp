// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a Wave 0, task W0-A (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md §2, §3, §8).
//
// Every acceptance here is a closed-form identity. There is no golden vector
// in this lane and none may be added.
#include "rta/meter/Block.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::meter;

namespace {

/// A block at a stated mean-square level, with a stated sample count. Built
/// from the closed form (`sumSquares = n * 10^(L/10)`), never from anything an
/// accumulator produced -- CLAUDE.md's verification standard.
Block blockAtLevel(std::uint64_t index, std::uint32_t samples, double levelDb) {
    Block b;
    b.blockIndex = index;
    b.blockSamples = samples;
    b.sumSquares = static_cast<double>(samples) * std::pow(10.0, levelDb / 10.0);
    return b;
}

constexpr std::uint32_t mask(BlockFlag f) { return static_cast<std::uint32_t>(f); }

}  // namespace

// --- A1: the layout §4's ring table is sized on -------------------------

TEST_CASE("A1 Block is 40 bytes with droppedSamples in the padding", "[block]") {
    // Compiled, not assumed: these two static_asserts are in Block.h itself,
    // so a compiler that disagrees fails the BUILD. Repeated here so the
    // failure also names this test file.
    static_assert(sizeof(Block) == 40, "sizeof(Block) != 40 -- record §4's ring table moves with it");
    static_assert(alignof(Block) == 8, "alignof(Block) != 8");

    // 8 + 4 + 4 + 8 + 4 + 4 + 4 + 4 == 40. `droppedSamples` occupies exactly
    // the four bytes the double's alignment was already wasting, so the
    // record's 36-of-40 becomes 40-of-40 and no ring figure moves.
    INFO("offsetof blockIndex     = " << offsetof(Block, blockIndex));
    INFO("offsetof blockSamples   = " << offsetof(Block, blockSamples));
    INFO("offsetof droppedSamples = " << offsetof(Block, droppedSamples));
    INFO("offsetof sumSquares     = " << offsetof(Block, sumSquares));
    INFO("offsetof maxFastDb      = " << offsetof(Block, maxFastDb));
    INFO("offsetof maxSlowDb      = " << offsetof(Block, maxSlowDb));
    INFO("offsetof peakDb         = " << offsetof(Block, peakDb));
    INFO("offsetof flags          = " << offsetof(Block, flags));
    INFO("sizeof(Block)           = " << sizeof(Block));
    CHECK(offsetof(Block, droppedSamples) == 12);
    CHECK(offsetof(Block, sumSquares) == 16);
    CHECK(sizeof(Block) == 40);
    CHECK(alignof(Block) == 8);
}

// --- A2: a block's Leq is the block's own energy ------------------------

TEST_CASE("A2 block Leq is 20log10(a) for a constant amplitude", "[block]") {
    // 1e-9, NOT 1e-12: summing 48 000 individual double squares accumulates
    // about 1e-12 of round-off against the single-multiplication closed form.
    // core/tests/test_leq.cpp:69-74 already widened this exact comparison and
    // states the same reason. The tolerance follows the algorithm; a builder
    // who wants 1e-12 back must put Kahan/Neumaier summation in
    // BlockAccumulator and say so.
    const std::uint32_t n = 48000;
    for (double a : {0.1, 0.3, 0.5, 1.0 / std::sqrt(2.0)}) {
        BlockAccumulator acc(n, 48000.0);
        std::vector<float> x(n, static_cast<float>(a));
        REQUIRE(acc.push(x, x) == x.size());
        const auto block = acc.poll();
        REQUIRE(block.has_value());
        const double af = static_cast<double>(static_cast<float>(a));
        const double leq = 10.0 * std::log10(block->sumSquares / block->blockSamples);
        INFO("a = " << a << "  residual = " << (leq - 20.0 * std::log10(af)));
        CHECK_THAT(leq, WithinAbs(20.0 * std::log10(af), 1e-9));
    }

    SECTION("the one exact case, asserted BITWISE") {
        // 48 000 copies of 1.0f square to 1.0 and sum exactly (integers up to
        // 2^53 are exact in double), so sumSquares/blockSamples is exactly 1.0
        // and both sides are exactly 0.0. No tolerance is needed and none is
        // given -- a tolerance here would hide a real arithmetic change.
        //
        // DEVIATION FROM THE PLAN, MEASURED NOT ARGUED: W0-A A2 also predicted
        // `a = 1/sqrt(2)` would land at 8.882e-16 and could be asserted
        // tightly beside a = 1. On MSVC 14.51 / x64 Release it lands at
        // 1.955e-12 -- three orders above that, and ABOVE 1e-12 as well. It is
        // therefore in the 1e-9 group above with its residual printed, for
        // exactly the reason A2 states about a = 0.1 and a = 0.3: 1/sqrt(2)
        // rounded to float32 does not square to a value whose 48 000-term sum
        // is exact, while 1.0f does. Only the integer case is exact.
        BlockAccumulator acc(n, 48000.0);
        std::vector<float> x(n, 1.0f);
        REQUIRE(acc.push(x, x) == x.size());
        const auto block = acc.poll();
        REQUIRE(block.has_value());
        const double leq = 10.0 * std::log10(block->sumSquares / block->blockSamples);
        CHECK(leq == 0.0);
        CHECK(block->sumSquares == static_cast<double>(n));
    }
}

// --- A3: the clock is samples, never hops -------------------------------

TEST_CASE("A3 the block clock is a sample count, not a hop boundary", "[block]") {
    const std::uint32_t blockSamples = 4800;
    const std::size_t hop = 1000;  // deliberately NOT a divisor of 4800
    BlockAccumulator acc(blockSamples, 48000.0);

    std::vector<float> hopBuf(hop, 0.25f);
    std::vector<Block> out;
    const std::size_t total = 3 * static_cast<std::size_t>(blockSamples) + 17;
    std::size_t fed = 0;
    while (fed < total) {
        const std::size_t n = std::min(hop, total - fed);
        std::size_t off = 0;
        while (off < n) {
            off += acc.push(std::span<const float>(hopBuf).subspan(off, n - off),
                            std::span<const float>(hopBuf).subspan(off, n - off));
            while (auto b = acc.poll()) out.push_back(*b);
        }
        fed += n;
    }

    REQUIRE(out.size() == 3);
    for (std::size_t i = 0; i < out.size(); ++i) {
        CHECK(out[i].blockIndex == i);
        CHECK(out[i].blockSamples == blockSamples);
    }
    CHECK(acc.pendingSamples() == 17);
}

// --- A4 / A4b: §3's energy sum, and the two fixtures a dB apart ---------

TEST_CASE("A4 combineBlocks sums energy -- halves at L and L+10", "[block]") {
    // Mean power = 0.5*10^(L/10) + 0.5*10^((L+10)/10) = 5.5*10^(L/10),
    // so Leq == L + 10*log10(5.5) == L + 7.4036268949.
    // Assert the FORMULA, never the literal: test_leq.cpp:41-47 catalogues
    // this repo's third wrong literal in a plan and an earlier revision of
    // this very row was the fourth -- it printed 10*log10(5.05), which is the
    // +-10 dB case in A4b, 0.371 dB away.
    const double L = 70.0;
    const double expected = L + 10.0 * std::log10(5.5);
    INFO("10*log10(5.5) = " << 10.0 * std::log10(5.5));

    for (std::size_t groups : {std::size_t{1}, std::size_t{2}, std::size_t{4}, std::size_t{900}}) {
        std::vector<Block> blocks;
        const std::uint32_t perBlock = 480;
        for (std::size_t i = 0; i < groups * 2; ++i) {
            blocks.push_back(blockAtLevel(i, perBlock, (i % 2 == 0) ? L : L + 10.0));
        }
        const auto r = combineBlocks(blocks, 48000.0, 0.0, blocks.size());
        REQUIRE(r.leqDb.has_value());
        INFO("groups = " << groups << "  residual = " << (*r.leqDb - expected));
        CHECK_THAT(*r.leqDb, WithinAbs(expected, 1e-9));
    }
}

TEST_CASE("A4b the +-10 dB case, named so the two are never confused", "[block]") {
    // Mean power = 0.5*10^-1 + 0.5*10^1 = 5.05 times 10^(L/10).
    const double L = 70.0;
    const double expected = L + 10.0 * std::log10(5.05);
    INFO("10*log10(5.05) = " << 10.0 * std::log10(5.05));
    INFO("gap to A4's 10*log10(5.5) = " << (10.0 * std::log10(5.5) - 10.0 * std::log10(5.05)));
    std::vector<Block> blocks;
    for (std::size_t i = 0; i < 900; ++i) {
        blocks.push_back(blockAtLevel(i, 480, (i % 2 == 0) ? L - 10.0 : L + 10.0));
    }
    const auto r = combineBlocks(blocks, 48000.0, 0.0, blocks.size());
    REQUIRE(r.leqDb.has_value());
    CHECK_THAT(*r.leqDb, WithinAbs(expected, 1e-9));
}

// --- A5: a recompute over current membership, never a subtraction -------

TEST_CASE("A5 the window is recomputed over current membership", "[block]") {
    std::vector<Block> blocks;
    for (std::size_t i = 0; i < 900; ++i) {
        blocks.push_back(blockAtLevel(i, 480, 60.0 + static_cast<double>(i % 7)));
    }
    blocks[400].flags |= mask(BlockFlag::CalibrationInvalid);

    std::vector<Block> survivors;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (i != 400) survivors.push_back(blocks[i]);
    }

    const auto full = combineBlocks(blocks, 48000.0, 0.0, 900);
    const auto fresh = combineBlocks(survivors, 48000.0, 0.0, 900);
    REQUIRE(full.leqDb.has_value());
    REQUIRE(fresh.leqDb.has_value());
    // BITWISE: the same 899 doubles in the same order. Anything looser would
    // hide a running-subtraction implementation, which is record §3's reason.
    CHECK(*full.leqDb == *fresh.leqDb);
    CHECK(full.blocks == 899);
    CHECK(full.excludedBlocks == 1);
}

// --- A6: bufferFill is honest, and an unfilled window has no Leq --------

TEST_CASE("A6 bufferFill reports the window it actually has", "[block]") {
    std::vector<Block> blocks;
    for (std::size_t i = 0; i < 300; ++i) blocks.push_back(blockAtLevel(i, 480, 60.0));
    const auto r = combineBlocks(blocks, 48000.0, 0.0, 900);
    CHECK_THAT(r.bufferFill, WithinAbs(1.0 / 3.0, 1e-12));
    REQUIRE(r.leqDb.has_value());

    const auto empty = combineBlocks({}, 48000.0, 0.0, 900);
    // ABSENT, never kLevelFloorDb: a live Leq shown without saying its window
    // is not yet full is a number that is quietly wrong (record §9).
    CHECK_FALSE(empty.leqDb.has_value());
    CHECK_THAT(empty.bufferFill, WithinAbs(0.0, 1e-15));
}

// --- A7: the calibration offset is one identity -------------------------

TEST_CASE("A7 calibrationOffsetDb is L_cal - L_meas", "[block]") {
    for (double m : {-3.0102999566398120, 0.0, 120.0}) {
        CHECK(calibrationOffsetDb(94.0, m) == 94.0 - m);
        CHECK(m + calibrationOffsetDb(94.0, m) == 94.0);  // bitwise round trip
    }
}

// --- A8: a gap is recoverable from the block alone ----------------------

TEST_CASE("A8 droppedSamples makes a gap reconstructible from the log", "[block]") {
    const std::uint32_t blockSamples = 4800;
    BlockAccumulator acc(blockSamples, 48000.0);
    std::vector<float> buf(blockSamples, 0.5f);
    std::vector<Block> out;

    for (int i = 0; i < 6; ++i) {
        if (i == 4) acc.noteDroppedSamples(12000);  // the bus lost 12 000 samples
        REQUIRE(acc.push(buf, buf) == buf.size());
        while (auto b = acc.poll()) out.push_back(*b);
    }
    REQUIRE(out.size() == 6);

    std::uint64_t elapsed = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const bool gapped = (i == 4);
        INFO("block " << i << " dropped = " << out[i].droppedSamples);
        CHECK(out[i].droppedSamples == (gapped ? 12000u : 0u));
        CHECK(((out[i].flags & mask(BlockFlag::Gap)) != 0) == gapped);
        elapsed += out[i].blockSamples + out[i].droppedSamples;
    }
    // Integers, no float: the true sample position after six blocks with one
    // 12 000-sample loss is exact.
    CHECK(elapsed == 6ull * blockSamples + 12000ull);
}

// --- A9..A13: which flags exclude, and every flag counted ---------------

namespace {

/// 900 identical blocks with `flag` set on exactly one of them.
std::vector<Block> flaggedRun(BlockFlag flag) {
    std::vector<Block> blocks;
    for (std::size_t i = 0; i < 900; ++i) blocks.push_back(blockAtLevel(i, 480, 85.0));
    blocks[400].flags |= mask(flag);
    return blocks;
}

std::vector<Block> cleanRun() {
    std::vector<Block> blocks;
    for (std::size_t i = 0; i < 900; ++i) blocks.push_back(blockAtLevel(i, 480, 85.0));
    return blocks;
}

}  // namespace

TEST_CASE("A9 CalibrationInvalid is the one flag that excludes", "[block]") {
    // The ONE exclusion with a published normative criterion behind it:
    // ISO 1996-2:2017 clause 5.2's discard rule.
    CHECK(excludesFromWindow(BlockFlag::CalibrationInvalid));
    const auto blocks = flaggedRun(BlockFlag::CalibrationInvalid);
    std::vector<Block> survivors;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (i != 400) survivors.push_back(blocks[i]);
    }
    const auto r = combineBlocks(blocks, 48000.0, 0.0, 900);
    const auto s = combineBlocks(survivors, 48000.0, 0.0, 900);
    REQUIRE(r.leqDb.has_value());
    CHECK(*r.leqDb == *s.leqDb);
    CHECK(r.excludedBlocks == 1);
    CHECK(r.blocks == 899);
}

TEST_CASE("A10..A12 Overload, UnderRange, Dropped and Gap all include", "[block]") {
    const auto clean = combineBlocks(cleanRun(), 48000.0, 0.0, 900);
    REQUIRE(clean.leqDb.has_value());

    // A10: a clipped waveform carries LESS energy than the signal that
    // clipped it, so dropping the block removes the loudest moment of the
    // show and biases the compliance number in the operator's favour.
    // A11: excluding floor readings biases Leq UP.
    // A12: a gapped block's energy is real over the samples it saw and
    // blockSamples is the honest divisor; what was lost is TIME (A8).
    for (BlockFlag f : {BlockFlag::Overload, BlockFlag::UnderRange, BlockFlag::Dropped,
                        BlockFlag::Gap}) {
        CHECK_FALSE(excludesFromWindow(f));
        const auto r = combineBlocks(flaggedRun(f), 48000.0, 0.0, 900);
        REQUIRE(r.leqDb.has_value());
        CHECK(*r.leqDb == *clean.leqDb);  // bitwise unchanged
        CHECK(r.blocks == 900);
        CHECK(r.excludedBlocks == 0);
    }
}

TEST_CASE("A13 every flag is counted whether it excludes or not", "[block]") {
    std::vector<Block> blocks = cleanRun();
    blocks[10].flags |= mask(BlockFlag::Overload);
    blocks[20].flags |= mask(BlockFlag::UnderRange);
    blocks[30].flags |= mask(BlockFlag::Gap);
    blocks[30].droppedSamples = 12000;
    blocks[40].flags |= mask(BlockFlag::Dropped);
    blocks[40].droppedSamples = 480;
    blocks[50].flags |= mask(BlockFlag::CalibrationInvalid);

    const auto r = combineBlocks(blocks, 48000.0, 0.0, 900);
    CHECK(r.overloadBlocks == 1);
    CHECK(r.underRangeBlocks == 1);
    CHECK(r.gapBlocks == 1);
    CHECK(r.droppedBlocks == 1);
    CHECK(r.excludedBlocks == 1);
    // droppedSamplesTotal counts the whole buffer, INCLUDING the excluded
    // block, because what it measures is elapsed time and not energy.
    CHECK(r.droppedSamplesTotal == 12480);
}
