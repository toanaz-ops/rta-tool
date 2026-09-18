// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a Wave 1, task W1-C (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md section 6).
//
// Every acceptance is a closed-form identity. The one thing this file spends
// most of its length on is proving a NEGATIVE -- that no hysteresis, debounce
// or margin is in the latch -- because record section 6's finding is that not
// one surveyed product publishes such a constant, so any this project shipped
// would be a number nobody can check.
#include "rta/meter/Alarm.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/meter/Block.h"

#include "BlockFixtures.h"
#include "support/SourceScan.h"

#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::meter;
using rta::testing::blockAtLevel;
using rta::testing::lowered;
using rta::testing::readRepoFile;
using rta::testing::stripLineComments;

namespace {

/// The identity under test, written out independently of the implementation:
/// the energy already spent is t*10^(L_t/10), the whole window's budget is
/// T*10^(L_lim/10), and the highest constant level that fills the remaining
/// T - t without exceeding the limit is 10*log10 of their difference over the
/// time left.
double referenceHeadroom(double windowSeconds, double elapsedSeconds, double elapsedLeqDb,
                         double limitDb) {
    const double budget = windowSeconds * std::pow(10.0, limitDb / 10.0);
    const double spent = elapsedSeconds * std::pow(10.0, elapsedLeqDb / 10.0);
    return 10.0 * std::log10((budget - spent) / (windowSeconds - elapsedSeconds));
}

/// A WindowResult carrying EXACTLY the level asked for.
///
/// Built as an aggregate rather than through combineBlocks on purpose: 60
/// blocks summed and divided lands within about 2.6e-15 dB of the level, and
/// one ULP of 100.0 dB is 1.42e-14 -- only five times larger. Routing C4a's
/// one-ULP swing through that arithmetic would make the fixture's amplitude
/// depend on summation round-off, which is precisely the kind of
/// too-well-behaved fixture the amplitude was chosen to avoid. A window built
/// from real blocks is exercised separately, below.
WindowResult windowAt(double levelDb) {
    WindowResult r;
    r.leqDb = levelDb;
    r.blocks = 60;
    r.samples = 60ull * 48000ull;
    r.seconds = 60.0;
    r.bufferFill = 1.0;
    return r;
}

/// Is `AlarmLatch::update` callable with these argument types at all? A
/// DELETED overload is found by name lookup and selected, so the expression is
/// ill-formed and this is false -- which is what makes the deletion a gate
/// rather than a comment.
template <class... Args>
concept Updatable = requires(AlarmLatch& latch, Args... args) { latch.update(args...); };

}  // namespace

// --- C1: the identity's fixed point --------------------------------------

TEST_CASE("C1 running AT the limit leaves exactly the limit as headroom", "[alarm]") {
    // The bound is DERIVED per triple, and the derivation is the interesting
    // part of this case.
    //
    // The bracket is (T*X - t*X)/(T - t) with X = 10^(L/10). Each product
    // carries at most 2 ULP of relative error -- one from std::pow, one from
    // the multiply -- so the numerator's ABSOLUTE error is about
    // 4*2^-53*T*X. The true numerator is (T - t)*X, so dividing amplifies the
    // relative error by T/(T - t): at t = 0.999*T that is a factor of ONE
    // THOUSAND. Carried through 10*log10 it is (10/ln 10) times the relative
    // error.
    //
    // A first version of this case used a flat "five roundings" bound of
    // 2.41e-15 dB and the measured worst was 6.25e-13 -- 260 times larger,
    // because it ignored the cancellation entirely. The identity genuinely
    // gets softer as the window fills, and that is worth knowing rather than
    // hiding under a round 1e-12: it is the same kind of error as a
    // sliding-window subtraction's, except bounded and recomputed.
    const double perUlp = 4.0 * std::pow(2.0, -53.0) * 10.0 / std::log(10.0);
    double worst = 0.0;
    double worstRatio = 0.0;
    for (double windowSeconds : {1.0, 3.0, 60.0, 900.0, 3600.0, 28800.0}) {
        for (double fraction : {0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 0.999}) {
            for (double limitDb : {85.0, 90.0, 94.0, 100.0, 102.0, 107.0}) {
                const double elapsed = windowSeconds * fraction;
                const double amplification = windowSeconds / (windowSeconds - elapsed);
                const double bound = perUlp * amplification;
                const auto h = headroomDb(windowSeconds, elapsed, limitDb, limitDb);
                INFO("T = " << windowSeconds << ", t = " << elapsed << ", L_lim = " << limitDb
                            << ", cancellation amplification " << amplification
                            << ", bound " << bound << " dB");
                REQUIRE(h.has_value());
                const double deviation = std::abs(*h - limitDb);
                CHECK(deviation <= bound);
                worst = std::max(worst, deviation);
                if (bound > 0.0) worstRatio = std::max(worstRatio, deviation / bound);
            }
        }
    }
    std::ostringstream report;
    report << std::setprecision(17) << "C1 worst |headroom - L_lim| = " << worst
           << " dB over 252 (T, t, L_lim) triples; worst fraction of its own derived "
           << "bound = " << worstRatio;
    WARN(report.str());
    // Even the worst triple is eleven orders under the 0.1 dB the display
    // shows, which is why the amplification is documented and not defended
    // against.
    CHECK(worst < 1e-11);

    SECTION("the one exactly-representable case, asserted BITWISE") {
        // 10^(90/10) = 1e9 exactly; 900*1e9 and 450*1e9 are both exact, their
        // difference is exact, and the quotient is 1e9 again. So this triple
        // has NO round-off to hide behind and the comparison can be bitwise.
        const auto h = headroomDb(900.0, 450.0, 90.0, 90.0);
        REQUIRE(h.has_value());
        CHECK(*h == 90.0);
    }

    SECTION("nothing spent yet: the whole window is available at the limit") {
        // t = 0 kills the cancellation outright -- the bracket is budget/T,
        // which is X back again -- so this one is bitwise too.
        const auto h = headroomDb(900.0, 0.0, 30.0, 100.0);
        REQUIRE(h.has_value());
        CHECK_THAT(*h, WithinAbs(100.0, 1e-13));
    }
}

// --- C2: the record's own check value ------------------------------------

TEST_CASE("C2 half the window spent 10 dB under the limit leaves L_lim + 10log10(1.9)",
          "[alarm]") {
    // Record section 6: at t = T/2 with L_t = L_lim - 10 the numerator is
    // 0.95*T*10^(L_lim/10), so the bracket is 1.9*10^(L_lim/10) and the answer
    // is L_lim + 10*log10(1.9). Asserted against the FORMULA, with the record's
    // printed 2.7875360 checked separately so a transcription error in either
    // place is caught rather than propagated.
    const double closedForm = 10.0 * std::log10(1.9);
    CHECK_THAT(closedForm, WithinAbs(2.7875360095, 1e-9));

    for (double windowSeconds : {3.0, 60.0, 900.0, 3600.0}) {
        for (double limitDb : {85.0, 90.0, 100.0, 107.0}) {
            const auto h =
                headroomDb(windowSeconds, windowSeconds / 2.0, limitDb - 10.0, limitDb);
            INFO("T = " << windowSeconds << ", L_lim = " << limitDb);
            REQUIRE(h.has_value());
            CHECK_THAT(*h, WithinAbs(limitDb + closedForm, 1e-12));
            CHECK_THAT(*h, WithinAbs(referenceHeadroom(windowSeconds, windowSeconds / 2.0,
                                                       limitDb - 10.0, limitDb),
                                     1e-12));
        }
    }
}

// --- C3: a lost window is an ABSENCE, not a threshold --------------------

TEST_CASE("C3 a window already over budget reports absence, never a floor", "[alarm]") {
    // 3.1 dB over the limit for half the window spends more than the whole
    // window's budget: the bracket goes negative, and "you cannot fix this by
    // turning down now" is a fact about the arithmetic rather than a threshold
    // somebody chose (memory/a-placeholder-for-an-absent-result-erases-its-
    // state.md; memory/a-threshold-read-off-a-grid-is-that-grids-floor.md).
    for (double limitDb : {85.0, 90.0, 100.0}) {
        INFO("L_lim = " << limitDb);
        CHECK_FALSE(headroomDb(900.0, 450.0, limitDb + 3.1, limitDb).has_value());
        // Made red by returning kLevelFloorDb or by clamping to L_lim: both
        // read as a measurement, and a caller cannot tell them from a real
        // -200 dB or a real "you may still run at the limit".
    }

    SECTION("the boundary is exactly 10log10(2) over, and it is absent too") {
        // At t = T/2, spending 10*log10(2) = 3.0103 dB over the limit uses
        // EXACTLY the whole budget: the bracket is zero, the remaining
        // allowance is -inf dB, and absent is the only honest answer.
        const double exactlyDouble = 10.0 * std::log10(2.0);
        const auto h = headroomDb(900.0, 450.0, 100.0 + exactlyDouble, 100.0);
        CHECK_FALSE(h.has_value());
    }

    SECTION("a hair under the boundary is still a number, and a very small one") {
        const double justUnder = 10.0 * std::log10(2.0) - 0.01;
        const auto h = headroomDb(900.0, 450.0, 100.0 + justUnder, 100.0);
        REQUIRE(h.has_value());
        CHECK(*h < 100.0);
        CHECK_THAT(*h, WithinAbs(referenceHeadroom(900.0, 450.0, 100.0 + justUnder, 100.0),
                                 1e-9));
    }

    SECTION("a window with no time left, and nonsense inputs, are absences too") {
        CHECK_FALSE(headroomDb(900.0, 900.0, 90.0, 100.0).has_value());
        CHECK_FALSE(headroomDb(900.0, 1200.0, 90.0, 100.0).has_value());
        CHECK_FALSE(headroomDb(0.0, 0.0, 90.0, 100.0).has_value());
        CHECK_FALSE(headroomDb(-900.0, 0.0, 90.0, 100.0).has_value());
        CHECK_FALSE(headroomDb(900.0, -1.0, 90.0, 100.0).has_value());
    }
}

// --- C4: no hysteresis, no debounce, no margin ---------------------------

TEST_CASE("C4a the latch transitions on EVERY crossing, at one ULP and at 0.01 dB",
          "[alarm]") {
    // THE AMPLITUDE IS THE WHOLE TEST. A plus-or-minus 3 dB swing would pass
    // with a 1 dB hysteresis quietly in place, so the swing has to be the
    // SMALLEST step that could be swallowed
    // (memory/a-fixture-can-be-too-well-behaved-to-fail.md, applied before it
    // bites rather than after). 0.01 dB is also a fifth of the 0.05 dB
    // semi-range IEC 61672-2 cl. 6.21 assigns a 0.1 dB display, so any
    // hysteresis small enough to pass it is finer than the instrument's own
    // resolution.
    const double limitDb = 100.0;
    const double oneUlp = std::nextafter(limitDb, std::numeric_limits<double>::infinity()) -
                          limitDb;
    INFO("one ULP of " << limitDb << " in double is " << oneUlp << " dB");
    REQUIRE(oneUlp > 0.0);

    for (double delta : {oneUlp, 0.01}) {
        INFO("delta = " << delta << " dB");
        AlarmLatch latch;
        CHECK_FALSE(latch.active());
        for (int step = 0; step < 40; ++step) {
            const bool over = (step % 2 == 0);
            const double value = over ? limitDb + delta : limitDb - delta;
            const AlarmLatch::Transition t = latch.update(windowAt(value), limitDb);
            INFO("step " << step << " value " << value);
            CHECK(t == (over ? AlarmLatch::Transition::Fired
                             : AlarmLatch::Transition::Cleared));
            CHECK(latch.active() == over);
        }
    }

    SECTION("repeating the same side is not a transition") {
        AlarmLatch latch;
        CHECK(latch.update(windowAt(101.0), limitDb) == AlarmLatch::Transition::Fired);
        CHECK(latch.update(windowAt(101.0), limitDb) == AlarmLatch::Transition::None);
        CHECK(latch.update(windowAt(105.0), limitDb) == AlarmLatch::Transition::None);
        CHECK(latch.active());
    }

    SECTION("exactly AT the limit is not over it") {
        // Record section 6: active while Leq(W) > L_lim. Equality is not an
        // exceedance, and that is the comparison, not a margin.
        AlarmLatch latch;
        CHECK(latch.update(windowAt(limitDb), limitDb) == AlarmLatch::Transition::None);
        CHECK_FALSE(latch.active());
    }

    SECTION("an absent windowed Leq is not compared at all") {
        AlarmLatch latch;
        REQUIRE(latch.update(windowAt(105.0), limitDb) == AlarmLatch::Transition::Fired);
        // combineBlocks over nothing leaves leqDb absent. A latch that treated
        // that as "below the limit" would CLEAR an alarm because the window
        // emptied, which is a state change the measurement did not report.
        const WindowResult empty = combineBlocks({}, 48000.0, 0.0, 60);
        REQUIRE_FALSE(empty.leqDb.has_value());
        CHECK(latch.update(empty, limitDb) == AlarmLatch::Transition::None);
        CHECK(latch.active());
    }
}

TEST_CASE("C4b the two files name no hysteresis, debounce, margin or dwell -- as CODE",
          "[alarm]") {
    // The naming half, and it is kept as NOTHING MORE than a naming check: a
    // latch holding a previousDb_ and an epsilon would match none of these
    // words, so this proves nothing behavioural. C4a is the proof.
    //
    // It scans the files with line comments STRIPPED, and the first version of
    // it did not -- it went red against this project's own header, which has to
    // say the words "hysteresis" and "debounce" in order to record WHY there
    // are none. A naming check that forbids a decision from being documented
    // is a check that trains the next author to delete the documentation.
    for (const char* file : {"core/include/rta/meter/Alarm.h", "core/src/meter/Alarm.cpp"}) {
        const std::string full = lowered(readRepoFile(file));
        const std::string code = lowered(stripLineComments(readRepoFile(file)));

        // The stripper has to be shown to work, or this whole case passes
        // vacuously on an empty string.
        REQUIRE(code.find("namespace rta::meter") != std::string::npos);
        REQUIRE(code.find("headroomdb") != std::string::npos);
        REQUIRE(code.size() < full.size());

        for (const char* forbidden : {"hyster", "debounce", "dwell", "margin"}) {
            INFO(file << " must not use " << forbidden << " in CODE");
            CHECK(code.find(forbidden) == std::string::npos);
        }
    }

    SECTION("and the decision IS documented, which is the other half of it") {
        // If a later change quietly drops the reasoning, this goes red. The
        // pair of checks is what makes the naming check safe to keep: the word
        // is banned from the code and REQUIRED in the prose.
        const std::string header = lowered(readRepoFile("core/include/rta/meter/Alarm.h"));
        CHECK(header.find("no hysteresis and no debounce") != std::string::npos);
    }
}

// --- C5: it compares a WINDOWED Leq, structurally ------------------------

TEST_CASE("C5 the latch cannot be handed a bare instantaneous level", "[alarm]") {
    // Structural, and enforced by the type system rather than by a grep: the
    // only update() that exists takes a WindowResult, which is produced by
    // combineBlocks alone. The double overload is DELETED, so a caller that
    // reaches for an instantaneous dB fails to COMPILE.
    static_assert(!Updatable<double, double>,
                  "AlarmLatch::update must not accept a bare instantaneous level");
    static_assert(Updatable<const WindowResult&, double>,
                  "AlarmLatch::update takes the windowed result");

    const std::string header = readRepoFile("core/include/rta/meter/Alarm.h");
    // The deleted overload is the gate; this asserts the gate is still there,
    // because removing it would make the static_assert above pass vacuously
    // through an implicit conversion nobody wrote.
    CHECK(header.find("= delete") != std::string::npos);

    SECTION("a window built from real blocks drives it the same way") {
        std::vector<Block> loud;
        for (std::size_t i = 0; i < 60; ++i) loud.push_back(blockAtLevel(i, 48000, 104.0));
        const WindowResult w = combineBlocks(loud, 48000.0, 0.0, 60);
        REQUIRE(w.leqDb.has_value());
        CHECK_THAT(*w.leqDb, WithinAbs(104.0, 1e-12));
        AlarmLatch latch;
        CHECK(latch.update(w, 100.0) == AlarmLatch::Transition::Fired);
        CHECK(latch.update(w, 107.0) == AlarmLatch::Transition::Cleared);
    }

    SECTION("reset returns the latch to clear without reporting a transition") {
        AlarmLatch latch;
        REQUIRE(latch.update(windowAt(110.0), 100.0) == AlarmLatch::Transition::Fired);
        latch.reset();
        CHECK_FALSE(latch.active());
        CHECK(latch.update(windowAt(110.0), 100.0) == AlarmLatch::Transition::Fired);
    }
}
