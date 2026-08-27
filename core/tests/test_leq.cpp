// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TDD sequence for meter/Leq -- see
// docs/plans/2026-08-27-weighting-meters-impl-plan.md section 7, Track L.
//
// Leq/SEL/peak come from a closed-form energy sum (IEC-style Leq definition);
// Ln (percentile) is a PROJECT CONVENTION, not a standard -- see the doc
// comment on percentileLevelDb() in Leq.h and
// docs/dsp/2026-08-27-weighting-and-meters.md. Every dB expectation below
// rests on a formula computed in the test, never on a typed-in literal that
// could silently carry a transcription error (CLAUDE.md's verification
// standard; this plan already caught two wrong literals elsewhere -- see
// test_detector.cpp).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/meter/Leq.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::meter;

TEST_CASE("Leq of a piecewise signal is the duty-weighted energy sum", "[leq]") {
    const double fs = 48000.0;

    SECTION("full-scale alternating +-1 for 1s -> mean square 1 -> 0 dB") {
        Leq leq(fs);
        std::vector<float> x(static_cast<std::size_t>(fs));
        for (std::size_t n = 0; n < x.size(); ++n) x[n] = (n % 2 == 0) ? 1.0f : -1.0f;
        leq.process(x);
        CHECK_THAT(leq.leqDb(), WithinAbs(0.0, 1e-12));
    }

    SECTION("two-level: half at a1=1.0 (0 dB), half at a2=0.1 (-20 dB)") {
        // NOTE: this project's plan (docs/plans/2026-08-27-weighting-meters-
        // impl-plan.md, L1) quotes the expected value here as "-2.7003 dB".
        // Independently re-derived: 10*log10(0.5*1.0^2 + 0.5*0.1^2) =
        // 10*log10(0.505) = -2.9671 dB, not -2.7003 -- a third wrong literal
        // in this plan (alongside the two already caught in test_detector.cpp
        // and plan section 12). Per CLAUDE.md's verification standard, assert
        // the FORMULA below, never the plan's typed-in literal.
        //
        // The amplitudes are also rounded to float32 (a1=1.0 is exact, a2=0.1
        // is not), and process() stores samples as float, so `expected` below
        // uses the SAME float-rounded amplitudes the detector actually sees --
        // otherwise a double-precision "expected" and a float-accumulated
        // "actual" disagree in the 9th decimal from amplitude quantization
        // alone, not from any bug.
        const double a1 = static_cast<double>(static_cast<float>(1.0));
        const double a2 = static_cast<double>(static_cast<float>(0.1));
        const std::size_t half = static_cast<std::size_t>(fs) / 2;
        Leq leq(fs);
        std::vector<float> seg1(half), seg2(half);
        for (std::size_t n = 0; n < half; ++n) {
            seg1[n] = (n % 2 == 0) ? static_cast<float>(a1) : static_cast<float>(-a1);
            seg2[n] = (n % 2 == 0) ? static_cast<float>(a2) : static_cast<float>(-a2);
        }
        leq.process(seg1);
        leq.process(seg2);
        const double expected = 10.0 * std::log10(0.5 * a1 * a1 + 0.5 * a2 * a2);
        // 1e-9, not 1e-12: summing 48000 individual double squares accumulates
        // ~1e-12 of round-off relative to the single-multiplication closed
        // form (same reasoning as the three-segment case below) -- this is
        // double-precision summation noise, not slack for a real bug.
        CHECK_THAT(leq.leqDb(), WithinAbs(expected, 1e-9));
    }

    SECTION("three unequal-duration segments") {
        struct Seg {
            double duty;
            double amp;
        };
        const Seg segs[] = {{0.2, 1.0}, {0.3, 0.5}, {0.5, 0.1}};
        Leq leq(fs);
        double expectedSum = 0.0;
        for (const auto& s : segs) {
            const auto n = static_cast<std::size_t>(fs * s.duty);
            // Same float-rounding note as the two-level case above: compare
            // against the amplitude the samples actually hold.
            const double amp = static_cast<double>(static_cast<float>(s.amp));
            std::vector<float> block(n);
            for (std::size_t i = 0; i < n; ++i) {
                block[i] = (i % 2 == 0) ? static_cast<float>(amp) : static_cast<float>(-amp);
            }
            leq.process(block);
            expectedSum += s.duty * amp * amp;
        }
        CHECK_THAT(leq.leqDb(), WithinAbs(10.0 * std::log10(expectedSum), 1e-9));
    }

    SECTION("referenceOffsetDb is additive and exact") {
        std::vector<float> x(4800);
        for (std::size_t n = 0; n < x.size(); ++n) x[n] = (n % 2 == 0) ? 1.0f : -1.0f;

        Leq noOffset(fs, 0.0);
        Leq withOffset(fs, 94.0);
        noOffset.process(x);
        withOffset.process(x);
        CHECK_THAT(withOffset.leqDb() - noOffset.leqDb(), WithinAbs(94.0, 1e-12));
    }

    SECTION("empty measurement floors, with no offset applied") {
        Leq leq(fs, 94.0);
        CHECK(leq.sampleCount() == 0u);
        CHECK(leq.leqDb() == kLevelFloorDb);
    }
}

TEST_CASE("Peak level is 10*log10(max(p^2)), not 10*log10(max|p|)", "[leq]") {
    const double fs = 48000.0;

    SECTION("amplitude 0.5, not 1.0 -- full scale hides the half-dB bug") {
        // 20*log10(0.5) = -6.0206 dB is correct. The half-dB bug,
        // 10*log10(0.5) = -3.0103 dB, is invisible at amplitude 1.0 (both
        // formulas read 0.0 dB there), which is why this fixture is 0.5.
        Leq leq(fs);
        std::vector<float> x(4800);
        for (std::size_t n = 0; n < x.size(); ++n) x[n] = (n % 2 == 0) ? 0.5f : -0.5f;
        leq.process(x);
        CHECK_THAT(leq.peakDb(), WithinAbs(20.0 * std::log10(0.5), 1e-12));
    }

    SECTION("sine crest factor: peakDb - leqDb == 10*log10(2) exactly") {
        // A whole number of periods makes the mean square exactly A^2/2, so
        // the crest factor is exact, not merely approximate.
        Leq leq(fs);
        const double amplitude = 0.7;
        const double cyclesPerSample = 100.0 / 4800.0;  // 100 whole periods in 4800 samples
        std::vector<float> x(4800);
        for (std::size_t n = 0; n < x.size(); ++n) {
            x[n] = static_cast<float>(
                amplitude *
                std::sin(2.0 * std::numbers::pi * cyclesPerSample * static_cast<double>(n)));
        }
        leq.process(x);
        CHECK_THAT(leq.peakDb() - leq.leqDb(), WithinAbs(10.0 * std::log10(2.0), 1e-6));
    }

    SECTION("negative-peak signal: max magnitude sits on a negative sample") {
        // A max(x) implementation (instead of max(|x|) / max(x*x)) passes
        // every symmetric fixture above and silently reports the wrong peak
        // here: the true peak magnitude is 0.9, carried by a NEGATIVE sample,
        // while the largest POSITIVE sample is only 0.3.
        Leq leq(fs);
        std::vector<float> x = {0.3f, -0.9f, 0.1f, -0.2f, 0.05f};
        leq.process(x);
        // Compare against the float32-rounded amplitude actually stored in
        // x[1] (same reasoning as the two-level Leq case above), not a
        // double literal that 0.9f cannot represent exactly.
        CHECK_THAT(leq.peakDb(), WithinAbs(20.0 * std::log10(static_cast<double>(-x[1])), 1e-12));
    }
}

TEST_CASE("SEL is Leq plus 10*log10(T/1s)", "[leq]") {
    const double fs = 48000.0;

    SECTION("10s unit-square (Leq=0dB) -> SEL=10dB") {
        Leq leq(fs);
        std::vector<float> x(static_cast<std::size_t>(10.0 * fs));
        for (std::size_t n = 0; n < x.size(); ++n) x[n] = (n % 2 == 0) ? 1.0f : -1.0f;
        leq.process(x);
        CHECK_THAT(leq.leqDb(), WithinAbs(0.0, 1e-9));
        CHECK_THAT(leq.selDb(), WithinAbs(10.0, 1e-9));
    }

    SECTION("0.1s measurement -> SEL = Leq - 10 dB (T < 1s SUBTRACTS)") {
        // The sign is where this gets written backwards: for T < 1s,
        // log10(T/1s) is NEGATIVE, so SEL reads BELOW Leq, not above it.
        Leq leq(fs);
        std::vector<float> x(static_cast<std::size_t>(0.1 * fs));
        for (std::size_t n = 0; n < x.size(); ++n) x[n] = (n % 2 == 0) ? 1.0f : -1.0f;
        leq.process(x);
        CHECK_THAT(leq.selDb(), WithinAbs(leq.leqDb() - 10.0, 1e-9));
    }
}

TEST_CASE("percentileLevelDb follows the project Ln convention", "[leq]") {
    SECTION("N=11 ramp 60..70, hand-computable positions") {
        std::vector<double> v(11);
        for (int i = 0; i <= 10; ++i) v[static_cast<std::size_t>(i)] = 60.0 + i;
        // pos = (11-1)*(1 - n/100)
        CHECK_THAT(percentileLevelDb(v, 10.0), WithinAbs(69.0, 1e-12));  // pos=9
        CHECK_THAT(percentileLevelDb(v, 50.0), WithinAbs(65.0, 1e-12));  // pos=5
        CHECK_THAT(percentileLevelDb(v, 90.0), WithinAbs(61.0, 1e-12));  // pos=1
    }

    SECTION("N=5, interpolated positions") {
        const std::vector<double> v = {50.0, 60.0, 70.0, 80.0, 90.0};
        // L10: pos = 4*0.9 = 3.6 -> v[3] + 0.6*(v[4]-v[3]) = 80 + 6 = 86
        CHECK_THAT(percentileLevelDb(v, 10.0), WithinAbs(86.0, 1e-12));
        // L90: pos = 4*0.1 = 0.4 -> v[0] + 0.4*(v[1]-v[0]) = 50 + 4 = 54
        CHECK_THAT(percentileLevelDb(v, 90.0), WithinAbs(54.0, 1e-12));
        // L50: pos = 4*0.5 = 2 -> v[2] = 70
        CHECK_THAT(percentileLevelDb(v, 50.0), WithinAbs(70.0, 1e-12));
    }

    SECTION("single-value input returns that value for any n") {
        const std::vector<double> v = {42.0};
        CHECK_THAT(percentileLevelDb(v, 0.0), WithinAbs(42.0, 1e-12));
        CHECK_THAT(percentileLevelDb(v, 50.0), WithinAbs(42.0, 1e-12));
        CHECK_THAT(percentileLevelDb(v, 100.0), WithinAbs(42.0, 1e-12));
    }

    SECTION("direction invariant: L10 > L50 > L90") {
        const std::vector<double> v = {40.0, 55.0, 58.0, 61.0, 63.0, 70.0, 90.0};
        CHECK(percentileLevelDb(v, 10.0) > percentileLevelDb(v, 50.0));
        CHECK(percentileLevelDb(v, 50.0) > percentileLevelDb(v, 90.0));
    }

    SECTION("input order does not matter -- shuffle-invariance") {
        std::vector<double> v = {61.2, 58.9, 70.0, 40.5, 55.5, 90.1, 63.3, 47.0};
        const double expected10 = percentileLevelDb(v, 10.0);
        const double expected50 = percentileLevelDb(v, 50.0);

        std::mt19937 rng(20260827);
        std::shuffle(v.begin(), v.end(), rng);
        CHECK(percentileLevelDb(v, 10.0) == expected10);
        CHECK(percentileLevelDb(v, 50.0) == expected50);
    }

    SECTION("throws on empty input and out-of-range n") {
        CHECK_THROWS_AS(percentileLevelDb({}, 50.0), std::invalid_argument);
        const std::vector<double> v = {1.0, 2.0};
        CHECK_THROWS_AS(percentileLevelDb(v, -1.0), std::invalid_argument);
        CHECK_THROWS_AS(percentileLevelDb(v, 101.0), std::invalid_argument);
    }
}

TEST_CASE("Ln is taken from the time-weighted level at 100 ms, not from raw samples",
          "[leq]") {
    const double fs = 48000.0;

    SECTION("Case A: settled staircase, exact plateaus") {
        // Five 4s segments (32*tau each, tau=0.125s Fast) of alternating +-A
        // at levels 60,70,80,90,60 dB. 20s @ 48kHz -> 200 history samples at
        // 100ms (historyIntervalSamples = 4800).
        //
        // Counting argument, independently verified against a Python
        // simulation of this exact recursion (not hand-waved): the level
        // history is NOT perfectly flat within a segment -- each segment's
        // first ~2s (the tail of a 0.125s-tau exponential settling toward a
        // NEW target) is a transient between the previous and new plateau,
        // and only the back half of each 40-sample segment is settled. The
        // simulation shows the specific ORDER STATISTICS these three
        // percentile positions land on are settled to <5e-7 dB of their
        // plateau:
        //   pos(L10) = 199*0.9 = 179.1 -> sorted[179..180] ~= 89.9999996 (90 dB run)
        //   pos(L90) = 199*0.1 =  19.9 -> sorted[19..20]   ~= 59.9999998 (60 dB run)
        //   pos(L50) = 199*0.5 =  99.5 -> sorted[99..100]  ~= 69.9999999 (70 dB run)
        // all comfortably inside WithinAbs(target, 1e-6).
        Leq leq(fs, 0.0, TimeWeighting::Fast);
        const double levels[] = {60.0, 70.0, 80.0, 90.0, 60.0};
        const auto segSamples = static_cast<std::size_t>(4.0 * fs);
        for (double L : levels) {
            const auto amp = static_cast<float>(std::pow(10.0, L / 20.0));
            std::vector<float> seg(segSamples);
            for (std::size_t n = 0; n < segSamples; ++n) seg[n] = (n % 2 == 0) ? amp : -amp;
            leq.process(seg);
        }

        CHECK(leq.levelHistory().size() == 200u);
        CHECK(leq.historyIntervalSamples() == 4800u);
        CHECK_THAT(leq.percentileDb(10.0), WithinAbs(90.0, 1e-6));
        CHECK_THAT(leq.percentileDb(90.0), WithinAbs(60.0, 1e-6));
        CHECK_THAT(leq.percentileDb(50.0), WithinAbs(70.0, 1e-6));
        CHECK_THAT(leq.maxDb(), WithinAbs(90.0, 1e-6));
    }

    SECTION("Case B: the raw-sample fallacy -- bursty 2% duty signal") {
        // 30s @ 48kHz: 20ms of 90dB at the start of every 1s second, 50dB the
        // rest (2% duty). A percentile taken over RAW samples reads ~50dB
        // (independently simulated: raw-sample L10 sits at 50.0). The Fast
        // detector rises toward 90dB during each burst and decays at
        // 34.74dB/s, so most of the ten 100ms history samples per second sit
        // well above 50dB -- simulated detector-sourced L10 is ~75.8dB, and
        // peakDb is exactly 90dB (instantaneous, unweighted).
        Leq leq(fs, 0.0, TimeWeighting::Fast);
        const auto amp90 = static_cast<float>(std::pow(10.0, 90.0 / 20.0));
        const auto amp50 = static_cast<float>(std::pow(10.0, 50.0 / 20.0));
        const auto samplesPerSec = static_cast<std::size_t>(fs);
        const auto burstSamples = static_cast<std::size_t>(fs * 0.020);
        for (int sec = 0; sec < 30; ++sec) {
            std::vector<float> block(samplesPerSec);
            for (std::size_t n = 0; n < samplesPerSec; ++n) {
                const float amp = (n < burstSamples) ? amp90 : amp50;
                block[n] = (n % 2 == 0) ? amp : -amp;
            }
            leq.process(block);
        }

        CHECK(leq.percentileDb(10.0) > 65.0);
        CHECK(leq.peakDb() > leq.percentileDb(10.0));
    }
}
