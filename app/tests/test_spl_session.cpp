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

#include "rta/dsp/Weighting.h"

#include <array>
#include <cmath>
#include <string>
#include <cstdint>
#include <span>
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

// PR #29 round-4 fix item 1: round 3 made SplMeter::push() poll the
// accumulator INSIDE its segment loop, so ONE feedHop can now close far more
// than `BlockAccumulator::kReadyCapacity` (4) blocks -- a small `blockSeconds`
// relative to the hop size closes many blocks per push(). `Chain::newlyClosed`
// was still reserved to the OLD 4-slot bound, so it must grow (allocate) once
// a single feedHop closes more than 4 blocks on one channel.
TEST_CASE("feedHop allocates nothing when one hop closes many blocks (round-4 item 1)",
          "[splsession]") {
    std::vector<float> hop(2048, 0.2f);

    SECTION("blockSeconds = 0.005 (240 samples/block at 48 kHz) -- up to 8 blocks/push") {
        SplConfig config = shortBlockConfig();
        config.blockSeconds = 0.005;
        SplSession session;
        const int channels[] = {0};
        session.start(config, kFs, channels);

        std::size_t bytes = 0;
        {
            const rta::test::AllocationProbe probe;
            for (int i = 0; i < 30; ++i) session.feedHop(0, hop);
            bytes = probe.bytes();
        }
        INFO("bytes allocated by 30 feedHops at blockSeconds=0.005, hop=2048 = " << bytes);
        CHECK(bytes == 0);
        CHECK(session.blockCount(0) > 0);
    }

    SECTION("blockSeconds = 0.002 (96 samples/block at 48 kHz) -- up to 21 blocks/push") {
        SplConfig config = shortBlockConfig();
        config.blockSeconds = 0.002;
        SplSession session;
        const int channels[] = {0};
        session.start(config, kFs, channels);

        std::size_t bytes = 0;
        {
            const rta::test::AllocationProbe probe;
            for (int i = 0; i < 30; ++i) session.feedHop(0, hop);
            bytes = probe.bytes();
        }
        INFO("bytes allocated by 30 feedHops at blockSeconds=0.002, hop=2048 = " << bytes);
        CHECK(bytes == 0);
        CHECK(session.blockCount(0) > 0);
    }

    // LOW follow-up batch, item 1: the two SECTIONs above never exercise
    // `Chain::newlyClosed`'s real capacity, so shrinking its `reserve(SplMeter
    // ::kScratchSamples)` to e.g. `reserve(64)` still read `bytes == 0`.
    // blockSamples = 1 against a 4096-sample hop drives every segment to one
    // sample and one block, filling `SplMeter::readyBuffer_` to its own fixed
    // capacity (kScratchSamples) before the counted-Dropped fallback takes
    // over -- see SplSession.h's corrected comment on `newlyClosed` for why
    // that capacity, not a pigeonhole argument, is the real bound.
    SECTION("blockSamples = 1 (hop 4096) -- fills SplMeter::readyBuffer_ to its real capacity") {
        SplConfig config = shortBlockConfig();
        config.blockSeconds = 1e-6;  // blockSamplesFor rounds this down to 1.
        SplSession session;
        const int channels[] = {0};
        session.start(config, kFs, channels);

        std::vector<float> hop(4096, 0.2f);
        std::size_t bytes = 0;
        {
            const rta::test::AllocationProbe probe;
            session.feedHop(0, hop);
            bytes = probe.bytes();
        }
        INFO("bytes allocated by one feedHop at blockSamples=1, hop=4096 = " << bytes);
        CHECK(bytes == 0);
        // The real bound, exactly: readyBuffer_'s own fixed capacity, not a
        // handful of blocks a mutated small reserve would happen to survive.
        CHECK(session.blockCount(0) == rta::measure::SplMeter::kScratchSamples);
    }
}

// --- PR #29 round-3 fix pass step 2: blockSecondsTooSmall is ADVISORY, ----
// reported, and never silent --------------------------------------------

TEST_CASE("blockSecondsTooSmall reports, but does not refuse, an under-floor config",
         "[splsession]") {
    SplSession session;
    const int channels[] = {0};

    SECTION("the shipped 1 s default is comfortably above the floor") {
        session.start(shortBlockConfig(), kFs, channels);
        CHECK_FALSE(session.blockSecondsTooSmall());
        CHECK(session.running());
    }

    SECTION("blockSeconds = 0.002 at 48 kHz is BELOW the advisory floor, and still runs") {
        SplConfig config = shortBlockConfig();
        config.blockSeconds = 0.002;
        session.start(config, kFs, channels);
        CHECK(session.blockSecondsTooSmall());
        // Advisory, not a refusal: the session still runs, and still logs
        // the channel -- SplMeter's own (generously oversized) ready buffer
        // is what actually protects sample accounting, proven directly by
        // test_spl_meter.cpp's own Sigma(blockSamples+droppedSamples) case.
        CHECK(session.running());
        CHECK(session.logsChannel(0));
    }

    SECTION("stop() clears the flag for the next start()") {
        SplConfig tooSmall = shortBlockConfig();
        tooSmall.blockSeconds = 0.002;
        session.start(tooSmall, kFs, channels);
        REQUIRE(session.blockSecondsTooSmall());
        session.stop();
        session.start(shortBlockConfig(), kFs, channels);
        CHECK_FALSE(session.blockSecondsTooSmall());
    }
}

// --- one chain per DISTINCT weighting, and the reason it is not optional --

TEST_CASE("a C-weighted metric is served C-weighted numbers, not A-weighted ones",
          "[splsession]") {
    // THE DEFECT THIS CASE EXISTS FOR. `SplMeter` runs ONE weighting per
    // instance (W0-B), so a session publishing both LAeq and a C-weighted
    // level needs TWO chains on the same channel. An earlier revision of
    // SplSession built a single A-weighted meter per channel and read every
    // metric's window from it -- which would have served a C-weighted metric
    // A-weighted numbers and said nothing about it, with `SplConfig::metrics`
    // carrying a `weighting` field the code ignored.
    SplConfig config;
    config.blockSeconds = 0.1;
    config.metrics.push_back(rta::measure::SplMetricSpec{
        "LAeq", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, 4});
    config.metrics.push_back(rta::measure::SplMetricSpec{
        "LCeq", rta::dsp::WeightingType::C, rta::meter::TimeWeighting::Fast, 4});
    // A third metric naming a weighting ALREADY present shares that chain:
    // two metrics differ only in window length, and combineBlocks is a
    // recompute over whatever tail it is handed.
    config.metrics.push_back(rta::measure::SplMetricSpec{
        "LAeq_long", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Slow, 4});

    SplSession session;
    const int channels[] = {0};
    session.start(config, kFs, channels);
    REQUIRE(session.chainCount() == 2);
    CHECK(session.weightings()[0] == rta::dsp::WeightingType::A);
    CHECK(session.weightings()[1] == rta::dsp::WeightingType::C);

    // 100 Hz, where A and C differ by about 19.1 dB analytically -- far more
    // than any tolerance question. The energy in each chain's blocks must
    // differ accordingly, which is what proves the two filters really ran.
    std::vector<float> hop(4800);
    for (std::size_t n = 0; n < hop.size(); ++n) {
        const double t = static_cast<double>(n) / kFs;
        hop[n] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * 100.0 * t));
    }
    for (int i = 0; i < 6; ++i) session.feedHop(0, hop);

    const auto aWindow = session.window(0, rta::dsp::WeightingType::A);
    const auto cWindow = session.window(0, rta::dsp::WeightingType::C);
    REQUIRE(aWindow.size() >= 2);
    REQUIRE(cWindow.size() >= 2);

    const double aDb = 10.0 * std::log10(aWindow.back().sumSquares / aWindow.back().blockSamples);
    const double cDb = 10.0 * std::log10(cWindow.back().sumSquares / cWindow.back().blockSamples);
    const double expectedGap =
        rta::dsp::Weighting::analyticDb(100.0, rta::dsp::WeightingType::A)
        - rta::dsp::Weighting::analyticDb(100.0, rta::dsp::WeightingType::C);
    INFO("A-weighted block level = " << aDb);
    INFO("C-weighted block level = " << cDb);
    INFO("measured gap = " << (aDb - cDb) << ", analytic gap = " << expectedGap);
    // The gap is the WEIGHTING's, not this code's: asserted against
    // rta::dsp::Weighting's own analytic curve, loosely, because the digital
    // cascade only approximates it and the size of that approximation is that
    // class's property. What matters here is that the two chains are NOT the
    // same numbers.
    CHECK(aDb < cDb - 10.0);
    CHECK_THAT(aDb - cDb, WithinAbs(expectedGap, 1.0));

    // A weighting the config never named yields an EMPTY span -- how a caller
    // learns it asked for something absent, never a silent substitution.
    CHECK(session.window(0, rta::dsp::WeightingType::Z).empty());

    // And fillMetricWindows hands metric 1 the C chain, metrics 0 and 2 the A
    // chain. This is what buildSplBlockView reads.
    std::array<std::span<const rta::meter::Block>, 3> windows{};
    REQUIRE(session.fillMetricWindows(0, windows) == 3);
    CHECK(windows[0].data() == aWindow.data());
    CHECK(windows[1].data() == cWindow.data());
    CHECK(windows[2].data() == aWindow.data());
}

TEST_CASE("a gap rides every chain exactly once, never chainCount times",
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

    std::vector<float> block(4800, 0.2f);
    session.noteDropCount(0, 0);  // baseline
    session.feedHop(0, block);
    session.noteDropCount(0, 12000);
    session.feedHop(0, block);

    CHECK(rta::meter::hasFlag(session.flagsSeen(0), BlockFlag::Gap));
    // 12 000, NOT 24 000. The same loss is upstream of both filters, so both
    // chains record it -- but summing over chains would report it twice and
    // make a reconstructed timestamp LATE, which is the same defect the count
    // exists to prevent, in the other direction.
    CHECK(session.droppedSamplesTotal(0) == 12000);
}

// The metric-cap / fillMetricWindows cases (PR #17 verifier defect 1) that
// used to follow here moved to test_spl_session_metric_cap.cpp, and the
// newlyClosedBlocks-per-chain / fix round 2026-09-25 item 4 / M5 cases to
// test_spl_session_newly_closed.cpp (PR #29 round-3 fix pass step 6,
// 400-line hard cap).
