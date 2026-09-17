// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L6a task W0-D, the JUCE-free half (SPL-R1, SPL-R2): the session that
// owns the meters, the window, and the arithmetic that turns a stalled drain
// into a Gap WITH A LENGTH.
//
// This is deliberately OFF-build: the gap arithmetic is the part a log reader
// depends on, and it is provable with no bus, no thread and no sound card.
// test_spl_drain.cpp (ON) proves the TAP POSITION, which only a real
// AnalysisThread can show.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "AllocationProbe.h"

#include "measure/SplSession.h"

#include <cmath>
#include <cstdint>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::measure::SplConfig;
using rta::measure::SplSession;
using rta::meter::BlockFlag;

namespace {

constexpr double kFs = 48000.0;

SplConfig shortBlockConfig(std::uint64_t windowBlocks = 4) {
    SplConfig config;
    config.blockSeconds = 0.1;  // 4800 samples at 48 kHz
    config.metrics.push_back(rta::measure::SplMetricSpec{
        "L", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, windowBlocks});
    return config;
}

}  // namespace

TEST_CASE("a session logs only the channels it was given", "[splsession]") {
    SplSession session;
    CHECK_FALSE(session.running());
    CHECK(session.config() == nullptr);

    const int channels[] = {1, 9};
    session.start(shortBlockConfig(), kFs, channels);
    CHECK(session.running());
    REQUIRE(session.config() != nullptr);
    CHECK(session.logsChannel(1));
    // Route position 8 -- the first past kMaxTransferFunctions -- and its
    // channel 9 are both perfectly ordinary here. The SPL meters are NOT
    // bounded by that constant (W0-D D1b).
    CHECK(session.logsChannel(9));
    CHECK_FALSE(session.logsChannel(0));
    CHECK_FALSE(session.logsChannel(2));
    CHECK_FALSE(session.logsChannel(-1));
    CHECK_FALSE(session.logsChannel(1000));

    session.stop();
    CHECK_FALSE(session.running());
    CHECK_FALSE(session.logsChannel(1));
}

TEST_CASE("feedHop completes blocks on a sample-count clock", "[splsession]") {
    SplSession session;
    const int channels[] = {0};
    session.start(shortBlockConfig(), kFs, channels);

    std::vector<float> hop(1600, 0.2f);  // three hops per 4800-sample block
    for (int i = 0; i < 9; ++i) session.feedHop(0, hop);
    CHECK(session.blockCount(0) == 3);
    REQUIRE(session.latestBlock(0).has_value());
    CHECK(session.latestBlock(0)->blockIndex == 2);
    CHECK(session.latestBlock(0)->blockSamples == 4800);
    // A channel nothing logs reports nothing, rather than a zero that reads
    // as a measurement.
    CHECK(session.blockCount(5) == 0);
    CHECK_FALSE(session.latestBlock(5).has_value());
}

TEST_CASE("the gap delta rides the block, and the first reading is only a baseline",
          "[splsession]") {
    // SPL-R1 ∧ SPL-R2, defect 5. The live drop counter is in memory and never
    // reaches the file, so a log carrying only the `Gap` BIT would admit that
    // time was lost and be unable to say how much -- and every later
    // reconstructed timestamp would be permanently early. The COUNT rides the
    // block.
    SplSession session;
    const int channels[] = {0};
    session.start(shortBlockConfig(), kFs, channels);

    std::vector<float> block(4800, 0.2f);

    // A bus that had ALREADY dropped 500 000 samples before logging began has
    // not lost anything from THIS log: the first call is a baseline only.
    session.noteDropCount(0, 500000);
    session.feedHop(0, block);
    REQUIRE(session.blockCount(0) == 1);
    CHECK(session.droppedSamplesTotal(0) == 0);
    CHECK_FALSE(rta::meter::hasFlag(session.flagsSeen(0), BlockFlag::Gap));

    // Then the bus loses 12 000 samples.
    session.noteDropCount(0, 512000);
    session.feedHop(0, block);
    REQUIRE(session.blockCount(0) == 2);
    CHECK(session.droppedSamplesTotal(0) == 12000);
    CHECK(rta::meter::hasFlag(session.flagsSeen(0), BlockFlag::Gap));

    // A clean block afterwards adds nothing and -- crucially -- does NOT
    // erase the fact that a gap happened. flagsSeen is monotonic because the
    // log is the evidence.
    session.noteDropCount(0, 512000);
    session.feedHop(0, block);
    REQUIRE(session.blockCount(0) == 3);
    CHECK(session.droppedSamplesTotal(0) == 12000);
    CHECK(rta::meter::hasFlag(session.flagsSeen(0), BlockFlag::Gap));

    // And elapsed samples is reconstructible from the window alone, in
    // integers: three 4800-sample blocks with one 12 000-sample loss.
    std::uint64_t elapsed = 0;
    for (const auto& b : session.window(0)) elapsed += b.blockSamples + b.droppedSamples;
    CHECK(elapsed == 3ull * 4800ull + 12000ull);
}

TEST_CASE("the window rolls at its capacity and stays contiguous and in order",
          "[splsession]") {
    SplSession session;
    const int channels[] = {0};
    session.start(shortBlockConfig(4), kFs, channels);

    std::vector<float> block(4800, 0.2f);
    for (int i = 0; i < 10; ++i) session.feedHop(0, block);

    CHECK(session.blockCount(0) == 10);
    const auto window = session.window(0);
    REQUIRE(window.size() == 4);
    // Oldest first, newest last, no gaps in the index sequence -- what
    // combineBlocks is handed directly.
    CHECK(window.front().blockIndex == 6);
    CHECK(window.back().blockIndex == 9);
    for (std::size_t i = 1; i < window.size(); ++i) {
        CHECK(window[i].blockIndex == window[i - 1].blockIndex + 1);
    }
    REQUIRE(session.latestBlock(0).has_value());
    CHECK(session.latestBlock(0)->blockIndex == 9);
}

TEST_CASE("feedHop allocates nothing once the session has started", "[splsession]") {
    SplSession session;
    const int channels[] = {0, 9};
    // start() ALLOCATES -- the meters and the windows -- and it is the message
    // thread's call. Everything after runs on the analysis thread inside a
    // live drain, where an allocation is a pause during a show.
    session.start(shortBlockConfig(4), kFs, channels);

    std::vector<float> hop(1600, 0.2f);
    std::size_t bytes = 0;
    {
        const rta::test::AllocationProbe probe;
        for (int i = 0; i < 60; ++i) {
            session.noteDropCount(0, static_cast<std::uint64_t>(i) * 3);
            session.feedHop(0, hop);
            session.feedHop(9, hop);
        }
        bytes = probe.bytes();
    }
    INFO("bytes allocated by 120 feedHops (20 blocks per channel) = " << bytes);
    CHECK(bytes == 0);
    CHECK(session.blockCount(0) == 20);
    CHECK(session.blockCount(9) == 20);
}
