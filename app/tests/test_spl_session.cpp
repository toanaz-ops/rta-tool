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

// --- the metric cap, and the hole it used to open above 16 ---------------

namespace {

/// `count` metrics, the LAST of which is C-weighted and every earlier one
/// A-weighted -- so a cap that drops the tail, or a fill that gives up and
/// lets the publish fall back, shows up as the C metric reading A numbers.
SplConfig manyMetrics(std::size_t count) {
    SplConfig config;
    config.blockSeconds = 0.1;
    for (std::size_t i = 0; i < count; ++i) {
        const bool last = (i + 1 == count);
        config.metrics.push_back(rta::measure::SplMetricSpec{
            last ? "LCeq_last" : ("LAeq_" + std::to_string(i)),
            last ? rta::dsp::WeightingType::C : rta::dsp::WeightingType::A,
            rta::meter::TimeWeighting::Fast, 4});
    }
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

TEST_CASE("the metric list is capped at construction, and the refusal is COUNTED",
          "[splsession]") {
    // DEFECT 1 FROM THE PR #17 VERIFIER. `SplConfig::metrics` was an unbounded
    // vector validated nowhere, while the publish path's per-metric window
    // storage is a fixed array of kMaxMetrics. At 17 metrics
    // `fillMetricWindows` returned 0 -- all-or-nothing -- so `metricWindows`
    // arrived EMPTY and every metric fell back to the first chain: the
    // C-weighted metric was published A-weighted numbers under a C label,
    // 18.8 dB wrong. That is e35f121's defect re-opened one index above the
    // array bound.
    //
    // Fixed three ways at once, and each one closes it alone:
    //   (a) the config is TRUNCATED to kMaxMetrics here, so the publish path
    //       cannot be handed more metrics than it has storage for;
    //   (b) fillMetricWindows fills the FIRST N instead of giving up; and
    //   (c) buildSplBlockView no longer falls back when metricWindows is
    //       non-empty but short -- a missing entry is ABSENCE.
    // Three layers because the failure was silent and 18.8 dB wide.
    SplSession session;
    const int channels[] = {0};

    SECTION("exactly at the cap: everything is accepted") {
        session.start(manyMetrics(SplConfig::kMaxMetrics), kFs, channels);
        REQUIRE(session.config() != nullptr);
        CHECK(session.config()->metrics.size() == SplConfig::kMaxMetrics);
        CHECK(session.refusedMetrics() == 0);
    }

    SECTION("one over the cap: TRUNCATED, and the count is published") {
        // TRUNCATION, not refusal of the whole session, and the choice is
        // deliberate: a misconfiguration that silenced SPL logging outright
        // would lose a show's evidence, which is worse than logging the first
        // sixteen. It is only acceptable BECAUSE the count is reported --
        // `refusedMetrics()` here, and `SplBlockView::refusedMetrics` in the
        // published snapshot, so nothing is dropped silently.
        session.start(manyMetrics(SplConfig::kMaxMetrics + 1), kFs, channels);
        REQUIRE(session.config() != nullptr);
        CHECK(session.config()->metrics.size() == SplConfig::kMaxMetrics);
        CHECK(session.refusedMetrics() == 1);
        // The dropped one is the LAST, and it is the C-weighted one -- so the
        // chain for C is not built either, and no surviving metric can be
        // handed C numbers or a C label by accident.
        for (const auto& spec : session.config()->metrics) {
            CHECK(spec.weighting == rta::dsp::WeightingType::A);
            CHECK(spec.id != "LCeq_last");
        }
    }

    SECTION("far over the cap") {
        session.start(manyMetrics(64), kFs, channels);
        REQUIRE(session.config() != nullptr);
        CHECK(session.config()->metrics.size() == SplConfig::kMaxMetrics);
        CHECK(session.refusedMetrics() == 64 - SplConfig::kMaxMetrics);
    }
}

TEST_CASE("fillMetricWindows fills the first N and says how many, never all-or-nothing",
          "[splsession]") {
    // The all-or-nothing return was the mechanism: a 0 meant "no per-metric
    // windows", which the publish path read as "single weighting, use the
    // shared window".
    SplSession session;
    const int channels[] = {0};
    session.start(manyMetrics(SplConfig::kMaxMetrics), kFs, channels);
    REQUIRE(session.config()->metrics.size() == SplConfig::kMaxMetrics);

    const auto hop = lowSine(4800);
    for (int i = 0; i < 6; ++i) session.feedHop(0, hop);

    SECTION("storage exactly the right size") {
        std::array<std::span<const rta::meter::Block>, SplConfig::kMaxMetrics> windows{};
        CHECK(session.fillMetricWindows(0, windows) == SplConfig::kMaxMetrics);
        // The last metric is C-weighted and must get the C chain, which is a
        // DIFFERENT buffer from the A chain the other fifteen share.
        const auto aWindow = session.window(0, rta::dsp::WeightingType::A);
        const auto cWindow = session.window(0, rta::dsp::WeightingType::C);
        REQUIRE_FALSE(aWindow.empty());
        REQUIRE_FALSE(cWindow.empty());
        CHECK(aWindow.data() != cWindow.data());
        CHECK(windows[0].data() == aWindow.data());
        CHECK(windows[SplConfig::kMaxMetrics - 1].data() == cWindow.data());
    }

    SECTION("storage SHORTER than the metric list fills what it can and reports it") {
        std::array<std::span<const rta::meter::Block>, 4> tooSmall{};
        CHECK(session.fillMetricWindows(0, tooSmall) == 4);
        const auto aWindow = session.window(0, rta::dsp::WeightingType::A);
        for (const auto& w : tooSmall) CHECK(w.data() == aWindow.data());
    }

    SECTION("a channel nothing logs fills nothing") {
        std::array<std::span<const rta::meter::Block>, SplConfig::kMaxMetrics> windows{};
        CHECK(session.fillMetricWindows(7, windows) == SplConfig::kMaxMetrics);
        // Filled, but with EMPTY spans: the count says how many rows were
        // written, not that any of them has data.
        for (const auto& w : windows) CHECK(w.empty());
    }
}

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
