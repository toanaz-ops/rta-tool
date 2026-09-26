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

    // Filling, not Clear (record §15 A6, round 4): no comparison has run yet.
    CHECK(alarm.report().state == rta::measure::SplAlarmState::Filling);
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
        // Filling, not Clear (record §15 A6, round 4): no comparison has run.
        CHECK(alarm.report().state == rta::measure::SplAlarmState::Filling);
    }
    CHECK(history.markers().empty());

    blocks.push_back(blockAtLevel(9, 480, 130.0));  // the 10th block: window is now full
    alarm.update(blocks, 48000.0, 1.0, 0.0, 9, history);
    CHECK(alarm.report().state == rta::measure::SplAlarmState::Fired);
    REQUIRE(history.markers().size() == 1);
}

// PR #26 round-5 fix: an all-excluded first full window must NOT flip
// Filling->Clear. `AlarmLatch::update` returns Transition::None both for "no
// comparison happened because leqDb is absent" (Alarm.h's own contract) and
// for "compared, and matched the latch's own already-false active_" -- the
// Filling->Clear promotion in SplAlarm::update was keyed on `transition ==
// None` alone, so it could not tell those two None cases apart, and an
// all-excluded window (leqDb absent) flipped to Clear as if it had been
// judged compliant. Record §15 A6 (20254ef): it must stay Filling until a
// window actually yields a value.

// Verifier probe P1: windowBlocks == 1 (the SplAlarmSpec default), the one
// and only block CalibrationInvalid.
TEST_CASE("an all-excluded window at windowBlocks==1 stays Filling (round-5, P1)",
         "[spl_alarms]") {
    SplAlarmSpec spec;
    spec.metricId = "LAeq,Fast";
    spec.limitDb = 90.0;
    // spec.windowBlocks left at its SplConfig.h default of 1.
    SplAlarm alarm(spec);
    SplHistory history(10);

    Block excluded = blockAtLevel(0, 480, 130.0);
    excluded.flags |= static_cast<std::uint32_t>(BlockFlag::CalibrationInvalid);
    std::vector<Block> blocks{excluded};
    alarm.update(blocks, 48000.0, 1.0, 0.0, 0, history);

    CHECK(alarm.report().state == rta::measure::SplAlarmState::Filling);
    CHECK(history.markers().empty());
}

// Verifier probe P2: windowBlocks == 3, all three blocks CalibrationInvalid.
TEST_CASE("an all-excluded full window at windowBlocks==3 stays Filling (round-5, P2)",
         "[spl_alarms]") {
    SplAlarmSpec spec;
    spec.metricId = "LAeq,Fast";
    spec.limitDb = 90.0;
    spec.windowBlocks = 3;
    SplAlarm alarm(spec);
    SplHistory history(10);

    std::vector<Block> blocks;
    for (std::uint64_t i = 0; i < 3; ++i) {
        Block excluded = blockAtLevel(i, 480, 130.0);
        excluded.flags |= static_cast<std::uint32_t>(BlockFlag::CalibrationInvalid);
        blocks.push_back(excluded);
        alarm.update(blocks, 48000.0, 1.0, 0.0, i, history);
    }

    CHECK(alarm.report().state == rta::measure::SplAlarmState::Filling);
    CHECK(history.markers().empty());
}

// The window AFTER an all-excluded full window, once it actually yields a
// value, still moves Filling->Clear or Filling->Fired correctly -- the fix
// must not disable the promotion forever, only defer it past a valueless
// window.
TEST_CASE("the window after an all-excluded one still resolves Filling correctly (round-5)",
         "[spl_alarms]") {
    SplAlarmSpec spec;
    spec.metricId = "LAeq,Fast";
    spec.limitDb = 90.0;
    // windowBlocks == 1: every update is its own full window, so the very
    // NEXT block (a real one) is the first window that ever yields a value.
    SplAlarm alarm(spec);
    SplHistory history(10);

    Block excluded = blockAtLevel(0, 480, 130.0);
    excluded.flags |= static_cast<std::uint32_t>(BlockFlag::CalibrationInvalid);
    std::vector<Block> blocks{excluded};
    alarm.update(blocks, 48000.0, 1.0, 0.0, 0, history);
    REQUIRE(alarm.report().state == rta::measure::SplAlarmState::Filling);  // sanity, per P1

    // A real, LOUD block: over the limit, so the first-ever real comparison
    // fires.
    blocks.push_back(blockAtLevel(1, 480, 130.0));
    alarm.update(blocks, 48000.0, 1.0, 0.0, 1, history);
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
        // Filling, not Clear (record §15 A6, round 4): no comparison has run.
        CHECK(report.state == rta::measure::SplAlarmState::Filling);
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
