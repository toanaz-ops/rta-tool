// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Split out of test_spl_session.cpp (PR #29 round-3 fix pass step 6,
// 400-line hard cap). Lane L6a task W0-D, fix round 2026-09-25 item 4 / M5:
// newlyClosedBlocks is PER CHAIN -- the verifier found no test anywhere
// referenced `newlyClosedBlocks` at all, so the literal M5 mutant ("feed
// `newlyClosed` from every chain") had nothing to fail against. These cases
// exercise the accessor directly.
//
// OFF-build, same reasoning as test_spl_session.cpp itself.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "measure/SplSession.h"

#include "rta/dsp/Weighting.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

using rta::measure::SplConfig;
using rta::measure::SplSession;

namespace {

constexpr double kFs = 48000.0;

SplConfig shortBlockConfig(std::uint64_t windowBlocks = 4) {
    SplConfig config;
    config.blockSeconds = 0.1;  // 4800 samples at 48 kHz
    config.metrics.push_back(rta::measure::SplMetricSpec{
        "L", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, windowBlocks});
    return config;
}

/// A 100 Hz sine, where A and C weighting differ by about 19 dB -- far more
/// than any tolerance question can blur.
std::vector<float> lowSine(std::size_t count) {
    std::vector<float> x(count);
    for (std::size_t n = 0; n < count; ++n) {
        const double t = static_cast<double>(n) / kFs;
        x[n] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * 100.0 * t));
    }
    return x;
}

}  // namespace

// --- fix round 2026-09-25 item 4 / M5: newlyClosedBlocks is PER CHAIN ------
//
// The verifier found no test anywhere referenced `newlyClosedBlocks` at all
// -- the literal M5 mutant ("feed `newlyClosed` from every chain") had
// nothing to fail against. These cases exercise the accessor directly.

TEST_CASE("newlyClosedBlocks reads one chain's own blocks, never another chain's",
          "[splsession]") {
    SplConfig config;
    config.blockSeconds = 0.1;
    config.metrics.push_back(rta::measure::SplMetricSpec{
        "LAeq", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, 4});
    config.metrics.push_back(rta::measure::SplMetricSpec{
        "LCeq", rta::dsp::WeightingType::C, rta::meter::TimeWeighting::Fast, 4});

    SplSession session;
    const int channels[] = {0};
    session.start(config, kFs, channels);
    REQUIRE(session.chainCount() == 2);

    const auto hop = lowSine(4800);  // exactly one 4800-sample block
    session.feedHop(0, hop);

    const auto aClosed = session.newlyClosedBlocks(0, rta::dsp::WeightingType::A);
    const auto cClosed = session.newlyClosedBlocks(0, rta::dsp::WeightingType::C);
    REQUIRE(aClosed.size() == 1);
    REQUIRE(cClosed.size() == 1);
    // Same blockIndex (both chains close in lockstep on the same hop) but
    // DIFFERENT energy -- the M5 mutant ("feed newlyClosed from every
    // chain", i.e. every chain reporting chain 0's own blocks) cannot pass
    // this: it would make the two spans equal.
    CHECK(aClosed[0].blockIndex == cClosed[0].blockIndex);
    CHECK(aClosed[0].sumSquares != cClosed[0].sumSquares);

    // A weighting this session never configured returns an EMPTY span, never
    // a silent alias onto a configured chain's blocks.
    CHECK(session.newlyClosedBlocks(0, rta::dsp::WeightingType::Z).empty());
}

TEST_CASE("a hop that closes several blocks reports each newly-closed block exactly once",
          "[splsession]") {
    SplSession session;
    const int channels[] = {0};
    session.start(shortBlockConfig(4), kFs, channels);  // 4800-sample blocks

    // 4 blocks, an arbitrary round number for this fixture's own subject
    // (per-chain `newlyClosedBlocks` reporting) -- NOT a ceiling. Before the
    // PR #29 round-3 step 2 fix, `rta::meter::BlockAccumulator::
    // kReadyCapacity` (4) bounded how many blocks a single `SplMeter::push`
    // could complete before a 5th block's worth in the same hop was
    // silently counted `Dropped`; `SplMeter` now drains the accumulator
    // inside its own segment loop (see that class's `readyBuffer_`), so a
    // hop can close far more than 4 blocks without losing any -- proven
    // directly by test_spl_meter.cpp's own step-2 Sigma(blockSamples +
    // droppedSamples) identity case.
    std::vector<float> bigHop(4800 * 4, 0.2f);  // 4 blocks in ONE hop/drain
    session.feedHop(0, bigHop);

    const auto closed = session.newlyClosedBlocks(0, rta::dsp::WeightingType::A);
    REQUIRE(closed.size() == 4);
    for (std::size_t i = 0; i < closed.size(); ++i) {
        CHECK(closed[i].blockIndex == i);
    }
    CHECK(session.blockCount(0) == 4);

    // A later hop that closes nothing (a partial block) reports an EMPTY
    // span -- not the previous hop's blocks left over.
    std::vector<float> smallHop(100, 0.2f);
    session.feedHop(0, smallHop);
    CHECK(session.newlyClosedBlocks(0, rta::dsp::WeightingType::A).empty());
}

TEST_CASE("48 hops crossing exactly one block boundary close it exactly once",
          "[splsession]") {
    // The coordinator's own "once-per-block" fixture: many small hops
    // accumulate toward ONE block boundary, and newlyClosedBlocks must show
    // it exactly once at the hop that crosses it -- never fewer, never
    // twice.
    SplSession session;
    const int channels[] = {0};
    session.start(shortBlockConfig(4), kFs, channels);  // 4800-sample blocks

    std::vector<float> hop(100, 0.2f);
    int closedCount = 0;
    bool sawClose = false;
    std::uint64_t seenIndex = 0;
    for (int i = 0; i < 48; ++i) {  // 48 * 100 = 4800 samples = exactly 1 block
        session.feedHop(0, hop);
        const auto closed = session.newlyClosedBlocks(0, rta::dsp::WeightingType::A);
        if (!closed.empty()) {
            REQUIRE(closed.size() == 1);
            CHECK_FALSE(sawClose);  // never reported twice
            sawClose = true;
            seenIndex = closed[0].blockIndex;
            ++closedCount;
        }
    }
    CHECK(closedCount == 1);
    CHECK(seenIndex == 0);
    CHECK(session.blockCount(0) == 1);
}

TEST_CASE("reconfiguring (stop then start) leaves no stale newlyClosed from the old session",
          "[splsession]") {
    SplSession session;
    const int channels[] = {0};
    session.start(shortBlockConfig(4), kFs, channels);

    std::vector<float> block(4800, 0.2f);
    session.feedHop(0, block);
    REQUIRE(session.newlyClosedBlocks(0, rta::dsp::WeightingType::A).size() == 1);

    session.stop();
    session.start(shortBlockConfig(4), kFs, channels);
    // A fresh session, before any feedHop: nothing closed yet -- not the
    // previous session's leftover block.
    CHECK(session.newlyClosedBlocks(0, rta::dsp::WeightingType::A).empty());
    CHECK(session.blockCount(0) == 0);

    session.feedHop(0, block);
    const auto closed = session.newlyClosedBlocks(0, rta::dsp::WeightingType::A);
    REQUIRE(closed.size() == 1);
    CHECK(closed[0].blockIndex == 0);  // block indices restart, not continue at 1
}

TEST_CASE("newlyClosedBlocks works per-weighting on a channel past kMaxTransferFunctions",
          "[splsession]") {
    SplConfig config;
    config.blockSeconds = 0.1;
    config.metrics.push_back(rta::measure::SplMetricSpec{
        "LAeq", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, 4});
    config.metrics.push_back(rta::measure::SplMetricSpec{
        "LCeq", rta::dsp::WeightingType::C, rta::meter::TimeWeighting::Fast, 4});

    SplSession session;
    const int channels[] = {9};  // route position 8, past kMaxTransferFunctions
    session.start(config, kFs, channels);

    std::vector<float> block(4800, 0.2f);
    session.feedHop(9, block);

    const auto aClosed = session.newlyClosedBlocks(9, rta::dsp::WeightingType::A);
    const auto cClosed = session.newlyClosedBlocks(9, rta::dsp::WeightingType::C);
    REQUIRE(aClosed.size() == 1);
    REQUIRE(cClosed.size() == 1);
    CHECK(session.newlyClosedBlocks(0, rta::dsp::WeightingType::A).empty());  // channel 0 untouched
}
