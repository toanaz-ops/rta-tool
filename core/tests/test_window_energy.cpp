// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a Wave 1, task W1-B (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md 3 and SPL-R3).
//
// Record 3's one decision: everything longer than a block is an ENERGY SUM
// over blocks, recomputed over the CURRENT membership, never a running
// subtraction. This file holds the whole of that claim -- the accessor that
// makes the energy readable (B1), the bitwise recompute (B2), the arithmetic
// that shows round-off is NOT the reason the subtraction was rejected (B2b),
// the mutable membership that IS the reason (B2c), and SEL (B3).
//
// It is its own file rather than an extension of test_block.cpp and
// test_leq.cpp because appending it to either would have pushed that file past
// CLAUDE.md's 400-line hard cap. The plan asked for the extension; the cap is
// the stronger rule, and the seam is clean: this is record 3, those are
// record 2 and the 2026-08-27 meter track.
#include "rta/meter/Block.h"
#include "rta/meter/Leq.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "BlockFixtures.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::meter;
using rta::testing::blockAtLevel;
using rta::testing::mask;

// --- B1: sumSquares beside leqDb (record 3, 10; SPL-R3) ------------------

TEST_CASE("B1 sumSquares sits beside leqDb, and the two agree bitwise", "[leq]") {
    // The accessor exists so a caller never has to invert a logarithm to get
    // the energy back. That is the same reason record §10 puts sumSquares in
    // the log file NEXT TO the rounded dB: a reader holding only the dB cannot
    // reproduce the report's own numbers, and a caller holding only leqDb()
    // cannot combine two measurements without re-exponentiating.
    const double fs = 48000.0;

    struct Signal {
        const char* name;
        double amplitude;
        std::size_t samples;
        double offset;
    };
    // Alternating +-a, so the mean square is exactly a*a and nothing in the
    // comparison depends on a sine's float quantisation (record §15 A4).
    const Signal signals[] = {
        {"unity, 1 s, no offset", 1.0, 48000, 0.0},
        {"unity, 1 s, +94 dB offset", 1.0, 48000, 94.0},
        {"half scale, 0.5 s", 0.5, 24000, 0.0},
        {"0.1, 2 s, +120 dB offset", 0.1, 96000, 120.0},
        {"0.001, 0.25 s", 0.001, 12000, 0.0},
        {"0.25, one 100 ms interval", 0.25, 4800, -13.5},
    };

    for (const Signal& s : signals) {
        INFO(s.name);
        Leq leq(fs, s.offset);
        std::vector<float> x(s.samples);
        const auto a = static_cast<float>(s.amplitude);
        for (std::size_t n = 0; n < x.size(); ++n) x[n] = (n % 2 == 0) ? a : -a;
        leq.process(x);

        REQUIRE(leq.sampleCount() == s.samples);
        // BITWISE: leqDb() is exactly this expression, so any difference would
        // mean the accessor is reporting a different accumulator.
        const double fromEnergy =
            10.0 * std::log10(leq.sumSquares() / static_cast<double>(leq.sampleCount())) +
            s.offset;
        CHECK(leq.leqDb() == fromEnergy);

        // And the energy itself is the closed form, not whatever was summed:
        // n * a^2 for an alternating +-a signal.
        //
        // The tolerance is RELATIVE and DERIVED, not typed. An absolute 1e-9
        // was the first thing written here and it was wrong: it failed on the
        // a = 0.1, 2 s row at sumSquares = 960.0000286, where the summed value
        // and the closed form differ by 2.3e-9 -- a relative 2.4e-12 against a
        // compensation-free bound of N*2^-53. The quantity is an energy whose
        // magnitude spans 960 down to 1.2e-5 across these six rows, so a fixed
        // absolute bound is the wrong SHAPE for it, not merely the wrong size
        // (memory/float32-fft-precision.md).
        const double aExact = static_cast<double>(a);
        const double closedForm = static_cast<double>(s.samples) * aExact * aExact;
        const double summationBound =
            closedForm * static_cast<double>(s.samples) * std::pow(2.0, -53.0);
        INFO("closed form " << closedForm << ", summed " << leq.sumSquares()
                            << ", N*2^-53 bound " << summationBound);
        CHECK_THAT(leq.sumSquares(), WithinAbs(closedForm, summationBound));
    }

    SECTION("no data: the energy is zero and says so, rather than flooring") {
        Leq leq(fs, 94.0);
        CHECK(leq.sumSquares() == 0.0);
        CHECK(leq.sampleCount() == 0u);
        // leqDb() floors WITHOUT the offset for "no data" (Leq.h:66-68); the
        // energy accessor has no floor to apply, which is why both exist.
        CHECK(leq.leqDb() == kLevelFloorDb);
    }

    SECTION("reset clears the energy with everything else") {
        Leq leq(fs);
        std::vector<float> x(4800, 1.0f);
        leq.process(x);
        REQUIRE(leq.sumSquares() > 0.0);
        leq.reset();
        CHECK(leq.sumSquares() == 0.0);
    }
}

TEST_CASE("B3 SEL is IEC 61672-1 cl. 3.12 Eq. (4): Leq + 10*log10(T_W / 1 s)", "[leq]") {
    const double fs = 48000.0;

    struct Case {
        const char* name;
        std::size_t samples;
        double seconds;
    };
    const Case cases[] = {
        {"T = 1 s -- SEL equals Leq exactly", 48000, 1.0},
        {"T = 0.1 s -- SEL reads 10 dB BELOW Leq", 4800, 0.1},
        {"T = 10 s -- SEL reads 10 dB above", 480000, 10.0},
        {"T = 0.5 s", 24000, 0.5},
    };

    for (const Case& c : cases) {
        INFO(c.name);
        Leq leq(fs);
        std::vector<float> x(c.samples);
        for (std::size_t n = 0; n < x.size(); ++n) x[n] = (n % 2 == 0) ? 1.0f : -1.0f;
        leq.process(x);

        // T_0 = 1 s is what makes SEL a NUMBER and not a rate; E_0 = p_0^2 T_0
        // with p_0 = 20 uPa and T_0 = 1 s (Eq. (4)), and Eq. (6) is its
        // inverse. The sign is the thing to get wrong: for T < 1 s the
        // logarithm is negative and SEL sits BELOW Leq.
        const double expected = leq.leqDb() + 10.0 * std::log10(c.seconds / 1.0);
        CHECK_THAT(leq.selDb(), WithinAbs(expected, 1e-12));
    }

    SECTION("the 0.1 s case, as the plain arithmetic it is") {
        Leq leq(fs);
        std::vector<float> x(4800);
        for (std::size_t n = 0; n < x.size(); ++n) x[n] = (n % 2 == 0) ? 1.0f : -1.0f;
        leq.process(x);
        // Mean square exactly 1.0, so Leq is exactly 0.0 dB and SEL is -10.
        CHECK_THAT(leq.leqDb(), WithinAbs(0.0, 1e-12));
        CHECK_THAT(leq.selDb(), WithinAbs(-10.0, 1e-12));
        CHECK(leq.selDb() < leq.leqDb());
    }
}

// --- B2: the sliding window is a RECOMPUTE, and why -----------------------

TEST_CASE("B2 a 900-block window recomputed equals a fresh sum, bitwise, at every step",
          "[block]") {
    // 900 blocks is record §3's own figure: a 15-minute compliance window at
    // 1 s blocks. 3600 steps is an hour of them rolling through.
    constexpr std::size_t kWindow = 900;
    constexpr std::size_t kSteps = 3600;
    constexpr double kFs = 48000.0;
    constexpr std::uint32_t kBlockSamples = 48000;

    const auto levelAt = [](std::size_t i) {
        // A level that moves, so a stale sum cannot pass by accident.
        return 80.0 + 20.0 * std::sin(0.01 * static_cast<double>(i));
    };

    std::vector<Block> ring(kWindow);
    for (std::size_t i = 0; i < kWindow; ++i) {
        ring[i] = blockAtLevel(i, kBlockSamples, levelAt(i));
    }

    double worstDelta = 0.0;
    for (std::size_t step = 0; step < kSteps; ++step) {
        const std::size_t slot = step % kWindow;
        ring[slot] =
            blockAtLevel(kWindow + step, kBlockSamples, levelAt(kWindow + step));

        const auto r = combineBlocks(ring, kFs, 0.0, kWindow);

        // The fresh computation, over THE SAME 900 doubles IN THE SAME ORDER.
        // Anything looser than bitwise here would hide a real difference:
        // there is no arithmetic in combineBlocks that this loop does not do.
        double sumSquares = 0.0;
        std::uint64_t samples = 0;
        for (const Block& b : ring) {
            sumSquares += b.sumSquares;
            samples += b.blockSamples;
        }
        const double fresh = 10.0 * std::log10(sumSquares / static_cast<double>(samples));
        REQUIRE(r.leqDb.has_value());
        REQUIRE(*r.leqDb == fresh);
        worstDelta = std::max(worstDelta, std::abs(*r.leqDb - fresh));
    }
    CHECK(worstDelta == 0.0);
}

TEST_CASE("B2b summing 900 doubles costs 4.3e-13 dB -- so ROUNDING is not why the "
          "running subtraction was rejected",
          "[block]") {
    // An earlier draft of record §3 rejected the O(1) running subtraction for
    // accumulated round-off, and that reason does not survive its own
    // arithmetic. Asserted as arithmetic rather than quoted: the relative
    // bound on N compensation-free additions is N*2^-53, and a relative error
    // eps on the mean square shows up as 10*log10(1+eps) in dB.
    const double n = 900.0;
    const double relative = n * std::pow(2.0, -53.0);
    const double inDb = 10.0 * std::log10(1.0 + relative);
    INFO("900 * 2^-53 = " << relative << " -> " << inDb << " dB");
    CHECK(relative < 1.1e-13);
    CHECK(inDb < 4.4e-13);
    // 0.1 dB is what the display shows (CLAUDE.md "dB keeps one decimal"), so
    // the round-off is ten orders of magnitude under the readout.
    CHECK(inDb * 1e10 < 0.1);
}

TEST_CASE("B2c the real reason: membership is mutable, so a subtraction cannot take "
          "back what it folded in",
          "[block]") {
    // 899 blocks at 90 dB and ONE at 130 dB. The loud block is then retired by
    // a calibration verdict -- which is the one thing record §15 A2 says
    // retires a block that is already inside the window.
    constexpr std::uint32_t kBlockSamples = 48000;
    std::vector<Block> window;
    window.reserve(900);
    for (std::size_t i = 0; i < 899; ++i) {
        window.push_back(blockAtLevel(i, kBlockSamples, 90.0));
    }
    window.push_back(blockAtLevel(899, kBlockSamples, 130.0));

    const auto before = combineBlocks(window, 48000.0, 0.0, 900);
    REQUIRE(before.leqDb.has_value());

    // What a running sum would hold: it added the loud block's energy once and
    // has no way to learn the block was later invalidated, because the block
    // never DEPARTS the window -- its membership changed under it.
    double runningSum = 0.0;
    std::uint64_t runningSamples = 0;
    for (const Block& b : window) {
        runningSum += b.sumSquares;
        runningSamples += b.blockSamples;
    }
    const double stale = 10.0 * std::log10(runningSum / static_cast<double>(runningSamples));
    CHECK(*before.leqDb == stale);

    window[899].flags |= mask(BlockFlag::CalibrationInvalid);
    const auto after = combineBlocks(window, 48000.0, 0.0, 900);
    REQUIRE(after.leqDb.has_value());
    CHECK(after.excludedBlocks == 1);

    // The recompute reads the CURRENT membership, so it reports the 899
    // survivors exactly. The stale running sum is still the old number.
    CHECK_THAT(*after.leqDb, WithinAbs(90.0, 1e-9));
    const double error = stale - *after.leqDb;
    INFO("a running subtraction would still report " << stale << " dB against the "
         << *after.leqDb << " dB the current membership gives: " << error << " dB out");
    // Ten decibels, not a rounding artefact -- which is the whole point. The
    // round-off B2b bounds is 4.3e-13 dB; this is thirteen orders larger, so
    // the rejection rests on mutable membership and not on drift.
    CHECK(error > 10.0);
    CHECK(error / (10.0 * std::log10(1.0 + 900.0 * std::pow(2.0, -53.0))) > 1e13);
}
