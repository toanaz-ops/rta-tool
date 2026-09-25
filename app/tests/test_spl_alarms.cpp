// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a Wave 2, task W2-B (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md §6, §13 Q5).
#include "measure/SplAlarms.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <random>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::measure::SplAlarm;
using rta::measure::SplAlarmReading;
using rta::measure::SplAlarms;
using rta::measure::SplAlarmSpec;
using rta::measure::SplHistory;
using rta::measure::SplMarkerKind;
using rta::measure::SplProxyWindow;
using rta::meter::Block;
using rta::meter::BlockFlag;

namespace {

Block blockAtLevel(std::uint64_t index, std::uint32_t samples, double levelDb) {
    Block b;
    b.blockIndex = index;
    b.blockSamples = samples;
    b.sumSquares = static_cast<double>(samples) * std::pow(10.0, levelDb / 10.0);
    return b;
}

/// A pseudo-random level signal, in [floorDb, ceilingDb], seeded so the fixture
/// is deterministic run to run (CLAUDE.md verification standard).
std::vector<Block> randomSignal(unsigned seed, std::size_t count, double floorDb, double ceilingDb) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(floorDb, ceilingDb);
    std::vector<Block> blocks;
    blocks.reserve(count);
    for (std::size_t i = 0; i < count; ++i) blocks.push_back(blockAtLevel(i, 480, dist(rng)));
    return blocks;
}

}  // namespace

// --- B1: sliding is the conservative policy ---------------------------------

TEST_CASE("B1 the sliding maximum is never below the consecutive-fixed maximum", "[spl_alarms]") {
    constexpr std::uint64_t kWindowBlocks = 900;  // "15 min" at 1 s blocks
    constexpr std::size_t kBlockCount = 3600;

    for (unsigned seed = 1; seed <= 5; ++seed) {
        const auto blocks = randomSignal(seed, kBlockCount, 40.0, 100.0);
        const auto sliding =
            rta::measure::slidingMaxLeqDb(blocks, 48000.0, 0.0, kWindowBlocks);
        const auto fixed =
            rta::measure::consecutiveFixedMaxLeqDb(blocks, 48000.0, 0.0, kWindowBlocks);
        REQUIRE(sliding.has_value());
        REQUIRE(fixed.has_value());
        INFO("seed " << seed << " sliding=" << *sliding << " fixed=" << *fixed);
        CHECK(*sliding >= *fixed);
    }
}

TEST_CASE("B1 sliding equals consecutive-fixed only for a constant signal", "[spl_alarms]") {
    std::vector<Block> constant;
    for (std::size_t i = 0; i < 3600; ++i) constant.push_back(blockAtLevel(i, 480, 85.0));
    const auto sliding = rta::measure::slidingMaxLeqDb(constant, 48000.0, 0.0, 900);
    const auto fixed = rta::measure::consecutiveFixedMaxLeqDb(constant, 48000.0, 0.0, 900);
    REQUIRE(sliding.has_value());
    REQUIRE(fixed.has_value());
    CHECK_THAT(*sliding, WithinAbs(*fixed, 1e-9));

    // And a varying signal is the row that PROVES the two computations are
    // actually different algorithms, not the same call renamed twice.
    const auto varying = randomSignal(7, 3600, 40.0, 100.0);
    const auto slidingV = rta::measure::slidingMaxLeqDb(varying, 48000.0, 0.0, 900);
    const auto fixedV = rta::measure::consecutiveFixedMaxLeqDb(varying, 48000.0, 0.0, 900);
    REQUIRE(slidingV.has_value());
    REQUIRE(fixedV.has_value());
    CHECK(*slidingV > *fixedV);
}

// --- B2: the proxy offset is the operator's, with no shipped default -------

TEST_CASE("B2 SplProxyWindow ships with no default margin", "[spl_alarms]") {
    SplProxyWindow proxy;
    // NOT 2.5 (the Pop Code's own "2-3 dB(A)" reading) and NOT the VLAREM
    // 102/100 pair's 2 dB gap -- either would be this project originating a
    // margin nobody published. The only default is none at all.
    CHECK(proxy.marginDb == 0.0);
    CHECK(proxy.targetWindowBlocks == 0);
    CHECK(proxy.proxyWindowBlocks == 0);

    // The operator's own setting, typed explicitly -- the class does not
    // reject or clamp it to either precedent.
    proxy.targetWindowBlocks = 3600;
    proxy.proxyWindowBlocks = 900;
    proxy.marginDb = 2.5;
    CHECK(proxy.marginDb == 2.5);
}

// --- B3: every transition is a marker ---------------------------------------

TEST_CASE("B3 a fire and a clear each append exactly one marker with the bitwise value", "[spl_alarms]") {
    SplAlarmSpec spec;
    spec.metricId = "LAeq,Fast";
    spec.limitDb = 90.0;
    spec.windowBlocks = 10;

    SplAlarm alarm(spec);
    SplHistory history(4096);

    std::vector<Block> blocks;
    // Ten quiet blocks: window never fills over the limit.
    for (std::uint64_t i = 0; i < 10; ++i) {
        blocks.push_back(blockAtLevel(i, 480, 70.0));
        alarm.update(blocks, 48000.0, 1.0, 0.0, i, history);
    }
    CHECK(history.markers().empty());
    CHECK(alarm.report().state == rta::measure::SplAlarmState::Clear);

    // Ten loud blocks: the sliding window crosses the limit and FIRES.
    double firedValueDb = 0.0;
    for (std::uint64_t i = 10; i < 20; ++i) {
        blocks.push_back(blockAtLevel(i, 480, 100.0));
        alarm.update(blocks, 48000.0, 1.0, 0.0, i, history);
        if (alarm.report().state == rta::measure::SplAlarmState::Fired && firedValueDb == 0.0) {
            firedValueDb = rta::meter::combineBlocks(
                              std::span<const Block>(blocks).last(spec.windowBlocks), 48000.0, 0.0,
                              spec.windowBlocks)
                              .leqDb.value();
        }
    }
    REQUIRE(history.markers().size() == 1);
    CHECK(history.markers()[0].kind == SplMarkerKind::Alarm);
    CHECK(history.markers()[0].direction == 1);
    CHECK(history.markers()[0].value == firedValueDb);  // BITWISE

    // Ten quiet blocks again: the window clears.
    for (std::uint64_t i = 20; i < 30; ++i) {
        blocks.push_back(blockAtLevel(i, 480, 70.0));
        alarm.update(blocks, 48000.0, 1.0, 0.0, i, history);
    }
    REQUIRE(history.markers().size() == 2);
    CHECK(history.markers()[1].kind == SplMarkerKind::Alarm);
    CHECK(history.markers()[1].direction == -1);
    CHECK(alarm.report().state == rta::measure::SplAlarmState::Clear);
}

// --- the latch is not evaluated on a partial window (PR #26 fix round 3) ---
// Alarm.h's own AlarmLatch::update contract: "An ABSENT leqDb ... is not
// compared at all". SplAlarm::update was feeding the latch a leqDb computed
// from whatever PARTIAL tail existed -- combineBlocks happily returns one
// even when far short of windowBlocks -- so a single loud block against a
// 900-block window fired immediately. The independent verifier's probe:
// exactly that, fired=1.
TEST_CASE("a single loud block does not fire a 900-block window", "[spl_alarms]") {
    SplAlarmSpec spec;
    spec.metricId = "LAeq,Fast";
    spec.limitDb = 90.0;
    spec.windowBlocks = 900;

    SplAlarm alarm(spec);
    SplHistory history(4096);

    std::vector<Block> blocks{ blockAtLevel(0, 480, 130.0) };  // one block, VERY loud
    alarm.update(blocks, 48000.0, 1.0, 0.0, 0, history);

    CHECK(alarm.report().state == rta::measure::SplAlarmState::Clear);
    CHECK(history.markers().empty());
    CHECK_FALSE(alarm.report().sinceBlock.has_value());
}

TEST_CASE("the latch fires only once the window actually holds windowBlocks blocks",
         "[spl_alarms]") {
    SplAlarmSpec spec;
    spec.metricId = "LAeq,Fast";
    spec.limitDb = 90.0;
    spec.windowBlocks = 10;

    SplAlarm alarm(spec);
    SplHistory history(4096);

    std::vector<Block> blocks;
    for (std::uint64_t i = 0; i < 9; ++i) {
        blocks.push_back(blockAtLevel(i, 480, 130.0));  // loud, but window is still partial
        alarm.update(blocks, 48000.0, 1.0, 0.0, i, history);
        CHECK(alarm.report().state == rta::measure::SplAlarmState::Clear);
    }
    CHECK(history.markers().empty());

    blocks.push_back(blockAtLevel(9, 480, 130.0));  // the 10th block: window is now full
    alarm.update(blocks, 48000.0, 1.0, 0.0, 9, history);
    CHECK(alarm.report().state == rta::measure::SplAlarmState::Fired);
    REQUIRE(history.markers().size() == 1);
}

// --- B4: headroomDb travels with the state ----------------------------------

TEST_CASE("B4 SplAlarmReading carries state, limitDb, windowBlocks, sinceBlock and headroomDb",
         "[spl_alarms]") {
    SplAlarmSpec spec;
    spec.metricId = "LAeq,Slow";
    spec.limitDb = 100.0;
    spec.windowBlocks = 10;

    SplAlarm alarm(spec);
    SplHistory history(4096);

    // A partial window (5 of 10 blocks), well under the limit: headroom is
    // PRESENT (there is time left, and budget exceeds what has been spent).
    std::vector<Block> blocks;
    for (std::uint64_t i = 0; i < 5; ++i) {
        blocks.push_back(blockAtLevel(i, 48000, 60.0));  // 1 s blocks
        alarm.update(blocks, 48000.0, 1.0, 0.0, i, history);
    }
    {
        const auto report = alarm.report();
        CHECK(report.metricId == spec.metricId);
        CHECK(report.limitDb == spec.limitDb);
        CHECK(report.windowBlocks == spec.windowBlocks);
        CHECK(report.state == rta::measure::SplAlarmState::Clear);
        CHECK_FALSE(report.sinceBlock.has_value());  // never transitioned yet
        REQUIRE(report.headroomDb.has_value());
    }

    // Fill the window completely. record §15 A6 (fix round item 4): once
    // full, headroom no longer freezes at "the window is already lost" --
    // t becomes T-Delta (Delta = one block's duration) and L_t is the Leq of
    // the most recent windowBlocks-1 blocks, i.e. the highest level the NEXT
    // block can take without the NEXT sliding window exceeding the limit.
    // Continuous with the filling phase above: same closed form, no new
    // constant, just a different (t, L_t) pair once the window can no longer
    // grow. See the "closed-form" test cases below for the two derivations
    // (steady state, and 10 dB under the limit) this number is checked
    // against.
    for (std::uint64_t i = 5; i < 10; ++i) {
        blocks.push_back(blockAtLevel(i, 48000, 60.0));
        alarm.update(blocks, 48000.0, 1.0, 0.0, i, history);
    }
    {
        const double T = static_cast<double>(spec.windowBlocks);
        const double delta = 1.0;
        const double recentLevelDb = 60.0;  // the most recent 9 of 10 blocks
        const double expected = 10.0 * std::log10(
            (T * std::pow(10.0, spec.limitDb / 10.0) -
             (T - delta) * std::pow(10.0, recentLevelDb / 10.0)) / delta);
        REQUIRE(alarm.report().headroomDb.has_value());
        CHECK_THAT(*alarm.report().headroomDb, WithinAbs(expected, 1e-9));
    }
}

// --- fix round item 4: headroomDb once the sliding window is full ----------
// record §15 A6 (orchestrator's design decision). Once full, t = T - Delta
// (Delta = one block's duration) and L_t is the Leq of the MOST RECENT
// windowBlocks-1 blocks -- the highest level the NEXT block can have so the
// NEXT sliding window does not exceed the limit. Continuous across the
// fill->full transition (same closed form throughout); no new constant.

namespace {
constexpr std::uint64_t kFullWindowBlocks = 900;  // T = 900 s at 1 s/block
constexpr double kFullWindowLimitDb = 100.0;

SplAlarmSpec fullWindowSpec() {
    SplAlarmSpec spec;
    spec.metricId = "LAeq,Fast";
    spec.limitDb = kFullWindowLimitDb;
    spec.windowBlocks = kFullWindowBlocks;
    return spec;
}
}  // namespace

TEST_CASE("closed-form (a): a full window steady at L_lim gives headroom == L_lim", "[spl_alarms]") {
    SplAlarm alarm(fullWindowSpec());
    SplHistory history(kFullWindowBlocks + 10);

    std::vector<Block> blocks;
    for (std::uint64_t i = 0; i < kFullWindowBlocks; ++i) {
        blocks.push_back(blockAtLevel(i, 48000, kFullWindowLimitDb));
        alarm.update(blocks, 48000.0, 1.0, 0.0, i, history);
    }
    REQUIRE(alarm.report().headroomDb.has_value());
    CHECK_THAT(*alarm.report().headroomDb, WithinAbs(kFullWindowLimitDb, 1e-9));
}

TEST_CASE("closed-form (b): a full window 10 dB under the limit gives the closed-form L_allow",
         "[spl_alarms]") {
    SplAlarm alarm(fullWindowSpec());
    SplHistory history(kFullWindowBlocks + 10);

    const double blockLevelDb = kFullWindowLimitDb - 10.0;
    std::vector<Block> blocks;
    for (std::uint64_t i = 0; i < kFullWindowBlocks; ++i) {
        blocks.push_back(blockAtLevel(i, 48000, blockLevelDb));
        alarm.update(blocks, 48000.0, 1.0, 0.0, i, history);
    }

    const double T = static_cast<double>(kFullWindowBlocks);
    const double delta = 1.0;
    // L_allow = 10*log10((T*10^(Llim/10) - (T-Delta)*10^((Llim-10)/10))/Delta)
    const double expected = 10.0 * std::log10(
        (T * std::pow(10.0, kFullWindowLimitDb / 10.0) -
         (T - delta) * std::pow(10.0, blockLevelDb / 10.0)) / delta);
    INFO("T=900 s, Delta=1 s, Llim=100: L_allow = " << expected);
    REQUIRE(alarm.report().headroomDb.has_value());
    CHECK_THAT(*alarm.report().headroomDb, WithinAbs(expected, 1e-9));
}

// PR #26 round-3 fix, item 1: the verifier's mutant M1 (`tail.subspan(0,
// W-1)` -- keep the OLDEST W-1 blocks instead of the most recent) survived
// every existing headroom fixture, because they are all UNIFORM: dropping
// the oldest or the newest of a constant signal gives the same Leq either
// way. A NON-uniform fixture is what makes "most recent" observable: block 0
// (the OLDEST, about to roll off) is very loud; blocks 1..9 (the newest 9,
// the ones the NEXT window actually keeps) are quiet. The expected value
// comes from the identity over those 9 quiet blocks ONLY -- if the oldest,
// loud block leaked in (M1's own bug), the result would be far lower.
TEST_CASE("closed-form: headroom uses the MOST RECENT windowBlocks-1 blocks, not the oldest",
         "[spl_alarms]") {
    SplAlarmSpec spec;
    spec.metricId = "LAeq,Fast";
    spec.limitDb = 100.0;
    spec.windowBlocks = 10;
    SplAlarm alarm(spec);
    SplHistory history(20);

    std::vector<Block> blocks;
    blocks.push_back(blockAtLevel(0, 48000, 120.0));  // OLDEST -- must be dropped
    alarm.update(blocks, 48000.0, 1.0, 0.0, 0, history);
    for (std::uint64_t i = 1; i < 10; ++i) {
        blocks.push_back(blockAtLevel(i, 48000, 60.0));  // the 9 NEWEST -- must be kept
        alarm.update(blocks, 48000.0, 1.0, 0.0, i, history);
    }

    const double T = 10.0;      // windowBlocks * blockSeconds
    const double delta = 1.0;
    const double recentLevelDb = 60.0;  // uniform over blocks 1..9
    const double expected = 10.0 * std::log10(
        (T * std::pow(10.0, spec.limitDb / 10.0) -
         (T - delta) * std::pow(10.0, recentLevelDb / 10.0)) / delta);
    REQUIRE(alarm.report().headroomDb.has_value());
    CHECK_THAT(*alarm.report().headroomDb, WithinAbs(expected, 1e-9));
}

// PR #26 round-3 fix, item 2: SplAlarmSpec::windowBlocks defaults to 1
// (SplConfig.h). windowBlocks == 1 means the window is FULL after the very
// first block, but "the most recent windowBlocks-1 blocks" is then an EMPTY
// span -- combineBlocks({}) has no samples, so its leqDb is absent, and the
// existing full-window branch's `if (recentResult.leqDb.has_value())` guard
// never fires. Headroom stayed nullopt forever for the tool's own default
// configuration. The identity itself says otherwise: at t=0, `spent = t *
// 10^(Lt/10)` is exactly 0 regardless of L_t, so the correct answer is
// exactly limitDb.
TEST_CASE("closed-form: windowBlocks == 1 (the SplAlarmSpec default) gives headroom == limitDb",
         "[spl_alarms]") {
    SplAlarmSpec spec;
    spec.metricId = "LAeq,Fast";
    spec.limitDb = 100.0;
    // spec.windowBlocks left at its SplConfig.h default of 1.
    SplAlarm alarm(spec);
    SplHistory history(4);

    std::vector<Block> blocks;
    blocks.push_back(blockAtLevel(0, 48000, 40.0));  // level is irrelevant: spent == 0 at t=0
    alarm.update(blocks, 48000.0, 1.0, 0.0, 0, history);

    REQUIRE(alarm.report().headroomDb.has_value());
    CHECK_THAT(*alarm.report().headroomDb, WithinAbs(spec.limitDb, 1e-9));
}

TEST_CASE("closed-form (c): when the recent window already exceeds budget, headroom is absent",
         "[spl_alarms]") {
    SplAlarm alarm(fullWindowSpec());
    SplHistory history(kFullWindowBlocks + 10);

    // Every block well ABOVE the limit: the most recent windowBlocks-1
    // blocks alone already spend more than the whole window's budget, so the
    // bracket is <= 0 -- "the window is already lost" is a fact about the
    // arithmetic (Alarm.h), never a floor or a clamp.
    std::vector<Block> blocks;
    for (std::uint64_t i = 0; i < kFullWindowBlocks; ++i) {
        blocks.push_back(blockAtLevel(i, 48000, kFullWindowLimitDb + 20.0));
        alarm.update(blocks, 48000.0, 1.0, 0.0, i, history);
    }
    CHECK_FALSE(alarm.report().headroomDb.has_value());
}

// PR #26 round-3 fix, item 3 (orchestrator decision): the filling branch
// already used MEASURED seconds (`windowed.seconds`, which combineBlocks
// computes only over non-excluded blocks) for `t`, so an exclusion inside a
// filling window silently vanished from `t` but NOT from the nominal `T` --
// the divisor stayed a flat `windowBlocks * blockSeconds` regardless. The
// full branch used a nominal `T - Delta` unconditionally, ignoring
// exclusions on both sides. Both are wrong for the same reason: `T` must be
// the divisor the NEXT compliance window will actually have, which is
// `windowSeconds` minus whatever has already been excluded (filling), or
// `s_recent + Delta` where `s_recent` is the MEASURED seconds of the kept
// "most recent windowBlocks-1 blocks" (full). With no exclusions, both
// reduce exactly to the pre-fix code, which is why every earlier test in
// this file is untouched.
TEST_CASE("closed-form: an excluded block inside a FULL window is dropped from both s and T",
         "[spl_alarms]") {
    SplAlarmSpec spec;
    spec.metricId = "LAeq,Fast";
    spec.limitDb = 100.0;
    spec.windowBlocks = 5;
    SplAlarm alarm(spec);
    SplHistory history(10);

    std::vector<Block> blocks;
    // Oldest (rolls off once full) -- irrelevant to headroom either way.
    blocks.push_back(blockAtLevel(0, 48000, 90.0));
    // The "most recent windowBlocks-1" span is blocks[1..4]. One of THOSE is
    // excluded, so the kept seconds/energy for the identity come from the
    // other three: index 1 is dropped, 2..4 (3 blocks, 3.0 s) at 70 dB.
    Block excluded = blockAtLevel(1, 48000, 130.0);
    excluded.flags |= static_cast<std::uint32_t>(BlockFlag::CalibrationInvalid);
    blocks.push_back(excluded);
    blocks.push_back(blockAtLevel(2, 48000, 70.0));
    blocks.push_back(blockAtLevel(3, 48000, 70.0));
    blocks.push_back(blockAtLevel(4, 48000, 70.0));
    for (std::uint64_t i = 0; i < blocks.size(); ++i) {
        std::vector<Block> soFar(blocks.begin(), blocks.begin() + static_cast<std::ptrdiff_t>(i) + 1);
        alarm.update(soFar, 48000.0, 1.0, 0.0, i, history);
    }

    const double sRecent = 3.0;   // 3 kept blocks * 1 s, the excluded one dropped
    const double delta = 1.0;     // one block's duration
    const double recentLevelDb = 70.0;
    const double expected = 10.0 * std::log10(
        ((sRecent + delta) * std::pow(10.0, spec.limitDb / 10.0) -
         sRecent * std::pow(10.0, recentLevelDb / 10.0)) / delta);
    REQUIRE(alarm.report().headroomDb.has_value());
    CHECK_THAT(*alarm.report().headroomDb, WithinAbs(expected, 1e-9));
}

TEST_CASE("closed-form: an excluded block inside a FILLING window shrinks T, not just t",
         "[spl_alarms]") {
    SplAlarmSpec spec;
    spec.metricId = "LAeq,Fast";
    spec.limitDb = 100.0;
    spec.windowBlocks = 5;  // window is NOT full at 3 blocks
    SplAlarm alarm(spec);
    SplHistory history(10);

    Block excluded = blockAtLevel(0, 48000, 130.0);
    excluded.flags |= static_cast<std::uint32_t>(BlockFlag::CalibrationInvalid);
    std::vector<Block> blocks;
    blocks.push_back(excluded);
    blocks.push_back(blockAtLevel(1, 48000, 80.0));
    blocks.push_back(blockAtLevel(2, 48000, 80.0));
    for (std::uint64_t i = 0; i < blocks.size(); ++i) {
        std::vector<Block> soFar(blocks.begin(), blocks.begin() + static_cast<std::ptrdiff_t>(i) + 1);
        alarm.update(soFar, 48000.0, 1.0, 0.0, i, history);
    }

    const double windowSeconds = 5.0;   // windowBlocks * blockSeconds
    const double excludedSecondsSoFar = 1.0;  // one excluded block so far
    const double T = windowSeconds - excludedSecondsSoFar;
    const double t = 2.0;  // measured seconds of the 2 kept blocks
    const double levelDb = 80.0;
    const double expected = 10.0 * std::log10(
        (T * std::pow(10.0, spec.limitDb / 10.0) - t * std::pow(10.0, levelDb / 10.0)) / (T - t));
    REQUIRE(alarm.report().headroomDb.has_value());
    CHECK_THAT(*alarm.report().headroomDb, WithinAbs(expected, 1e-9));
}

// PR #26 round-3 fix, item 4: the verifier's mutant M5 (breaking the
// `valueDb_` update at SplAlarms.cpp's `if (windowed.leqDb.has_value())
// valueDb_ = ...` line) survived every existing test, because none of them
// ever read `report().valueDb` -- only headroomDb and state were asserted.
TEST_CASE("closed-form: valueDb reports the windowed Leq the alarm actually computed",
         "[spl_alarms]") {
    SplAlarmSpec spec;
    spec.metricId = "LAeq,Fast";
    spec.limitDb = 100.0;
    spec.windowBlocks = 5;
    SplAlarm alarm(spec);
    SplHistory history(10);

    std::vector<Block> blocks;
    for (std::uint64_t i = 0; i < spec.windowBlocks; ++i) {
        blocks.push_back(blockAtLevel(i, 48000, 65.0));
        alarm.update(blocks, 48000.0, 1.0, 0.0, i, history);
    }
    CHECK_THAT(static_cast<double>(alarm.report().valueDb), WithinAbs(65.0, 1e-3));
}

TEST_CASE("B4 SplAlarms updates every configured alarm and collects their reports", "[spl_alarms]") {
    std::vector<SplAlarmSpec> specs;
    SplAlarmSpec a;
    a.metricId = "A";
    a.limitDb = 90.0;
    a.windowBlocks = 5;
    specs.push_back(a);
    SplAlarmSpec b;
    b.metricId = "B";
    b.limitDb = 95.0;
    b.windowBlocks = 5;
    specs.push_back(b);

    SplAlarms alarms(std::move(specs));
    SplHistory history(4096);
    std::vector<Block> blocks;
    for (std::uint64_t i = 0; i < 5; ++i) {
        blocks.push_back(blockAtLevel(i, 480, 92.0));
        alarms.update(blocks, 48000.0, 1.0, 0.0, i, history);
    }
    const auto reports = alarms.reports();
    REQUIRE(reports.size() == 2);
    CHECK(reports[0].metricId == "A");
    CHECK(reports[0].state == rta::measure::SplAlarmState::Fired);  // 92 > 90
    CHECK(reports[1].metricId == "B");
    CHECK(reports[1].state == rta::measure::SplAlarmState::Clear);  // 92 < 95
    // One marker each, since both alarms fired-or-cleared exactly once.
    CHECK(history.markers().size() == 1);  // only A transitioned
}

// PR #26 fix round item 6: "there must be one type for one fact". SplAlarms
// used to return its own SplAlarmReport, a near-duplicate of Snapshot.h's
// SplAlarmReading carrying the same state/limitDb/headroomDb plus two fields
// (windowBlocks, sinceBlock) that type didn't have. Those two fields now
// live on SplAlarmReading itself (plan B4 names this the one alarm-reading
// type), and SplAlarm::report() returns it directly.
TEST_CASE("SplAlarmReading itself carries windowBlocks and sinceBlock", "[spl_alarms]") {
    rta::measure::SplAlarmReading reading;
    reading.windowBlocks = 900;
    reading.sinceBlock = 42;
    CHECK(reading.windowBlocks == 900);
    REQUIRE(reading.sinceBlock.has_value());
    CHECK(*reading.sinceBlock == 42);
}
