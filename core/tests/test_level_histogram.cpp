// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a Wave 1, task W1-A (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md §5, and SPL-R8 for the base).
//
// Every acceptance here is a closed-form identity or the project's own
// existing Ln convention. There is no golden vector in this lane.
#include "rta/meter/LevelHistogram.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/meter/Leq.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::meter;

namespace {

/// The exact Ln of a raw sample set, through the project's ONE convention
/// (percentileLevelDb, Leq.h:28). The histogram is not allowed to invent a
/// second interpolation, so the reference here is the shipped function and
/// never a formula retyped in this file.
double exactPercentile(const std::vector<double>& levels, double n) {
    return percentileLevelDb(levels, n);
}

/// The same levels quantised the way the histogram quantises them: to the
/// centre of the bin they fall in. A2's theorem is about exactly this map.
std::vector<double> quantiseToCentres(const std::vector<double>& levels, double baseDb) {
    std::vector<double> out;
    out.reserve(levels.size());
    for (double v : levels) {
        const auto bin = static_cast<std::size_t>(
            std::floor((v - baseDb) / LevelHistogram::kBinWidthDb));
        out.push_back(baseDb + (static_cast<double>(bin) + 0.5) * LevelHistogram::kBinWidthDb);
    }
    return out;
}

constexpr double kBase = -20.0;  ///< A5's stated base; NOT a default (see the header)

/// Ten distributions, all inside [kBase, kBase + 200), named so a failure says
/// which shape broke the bound rather than which index.
std::vector<std::pair<std::string, std::vector<double>>> distributions() {
    std::mt19937 rng(20260918u);
    std::vector<std::pair<std::string, std::vector<double>>> out;

    std::vector<double> uniform;
    std::uniform_real_distribution<double> u(30.0, 130.0);
    for (int i = 0; i < 5000; ++i) uniform.push_back(u(rng));
    out.emplace_back("uniform 30..130", uniform);

    std::vector<double> bimodal;
    std::normal_distribution<double> lo(60.0, 2.0), hi(110.0, 3.0);
    for (int i = 0; i < 2500; ++i) bimodal.push_back(lo(rng));
    for (int i = 0; i < 2500; ++i) bimodal.push_back(hi(rng));
    out.emplace_back("bimodal 60/110", bimodal);

    std::vector<double> heavy;
    std::exponential_distribution<double> e(0.08);
    for (int i = 0; i < 5000; ++i) heavy.push_back(40.0 + std::min(e(rng), 130.0));
    out.emplace_back("heavy-tailed", heavy);

    out.emplace_back("single-valued", std::vector<double>(500, 93.37));

    // Two values astride ONE bin edge: the worst case the w/2 argument has to
    // survive, because each side quantises to a different centre.
    const double edge = kBase + 1234.0 * LevelHistogram::kBinWidthDb;
    std::vector<double> astride;
    for (int i = 0; i < 400; ++i) astride.push_back(edge - 1e-6);
    for (int i = 0; i < 400; ++i) astride.push_back(edge + 1e-6);
    out.emplace_back("two-valued astride a bin edge", astride);

    out.emplace_back("one sample", std::vector<double>{77.25});
    out.emplace_back("two samples", std::vector<double>{77.25, 121.75});

    std::vector<double> ramp;
    for (int i = 0; i < 2000; ++i) ramp.push_back(25.0 + 0.075 * static_cast<double>(i));
    out.emplace_back("linear ramp", ramp);

    std::vector<double> spiky(3000, 88.0);
    for (std::size_t i = 0; i < spiky.size(); i += 97) spiky[i] = 134.5;
    out.emplace_back("mostly flat with spikes", spiky);

    std::vector<double> nearFloor;
    std::normal_distribution<double> nf(kBase + 0.35, 0.05);
    for (int i = 0; i < 1500; ++i) nearFloor.push_back(std::max(nf(rng), kBase + 1e-6));
    out.emplace_back("pressed against the bottom of the span", nearFloor);

    return out;
}

}  // namespace

// --- A1: the shape, and why the counters are wide enough ------------------

TEST_CASE("A1 the histogram is 2000 bins of 0.1 dB plus two out-of-span counters",
          "[levelhistogram]") {
    static_assert(LevelHistogram::kBinCount == 2000, "record 5's NUM_STAT_BINS");
    static_assert(LevelHistogram::kBinWidthDb == 0.1, "record 5's 0.1 dB resolution");
    static_assert(LevelHistogram::kTableBytes == 8008,
                  "2000*4 + 2*4 -- the table the record sizes at 7.82 KiB");

    LevelHistogram h(kBase);
    CHECK(h.baseDb() == kBase);
    // The span is [base, base + 200) dB: 2000 bins of 0.1 dB.
    CHECK_THAT(h.topDb(), WithinAbs(kBase + 200.0, 1e-12));
    CHECK(h.total() == 0u);
    CHECK(h.belowSpan() == 0u);
    CHECK(h.aboveSpan() == 0u);

    // Counter sufficiency as ARITHMETIC, not as a comment: a uint32 bin at the
    // 10 Hz detector sampling rate record 5 assumes takes 13.6 years to
    // overflow, so a session cannot reach it.
    const double seconds = static_cast<double>(std::numeric_limits<std::uint32_t>::max()) / 10.0;
    CHECK(seconds == 429496729.5);
    const double years = seconds / (365.25 * 86400.0);
    INFO("uint32 bin at 10 samples/s overflows after " << years << " years");
    CHECK(years > 13.6);
}

// --- A3: bin CENTRES, which is the bias a floor-keyed bin carries ---------

TEST_CASE("A3 a single sample reports its bin CENTRE, not its bin edge",
          "[levelhistogram]") {
    for (std::size_t bin : {std::size_t{0}, std::size_t{1}, std::size_t{999},
                            std::size_t{1999}}) {
        const double edge = kBase + static_cast<double>(bin) * LevelHistogram::kBinWidthDb;
        LevelHistogram h(kBase);
        h.add(edge + 0.5 * LevelHistogram::kBinWidthDb);
        const double centre =
            kBase + (static_cast<double>(bin) + 0.5) * LevelHistogram::kBinWidthDb;
        INFO("bin " << bin << " edge " << edge << " centre " << centre);
        const LnResult r = h.percentile(50.0);
        REQUIRE(r.db.has_value());
        CHECK_THAT(*r.db, WithinAbs(centre, 1e-12));
        // The half-bin the centre convention adds is the whole point: dropping
        // it biases every Ln DOWN by 0.05 dB, which is what a floor-keyed bin
        // does (research B1.4, NoiseCapture).
        CHECK(*r.db > edge);
        CHECK_THAT(*r.db - edge, WithinAbs(0.05, 1e-12));
    }
}

// --- A2: the w/2 bound is a theorem, over every shape --------------------

TEST_CASE("A2 binned Ln is within half a bin of the exact Ln, always",
          "[levelhistogram]") {
    // The bound is w/2 = 0.05 dB from record 5's monotone-map-plus-convex-
    // combination argument: quantising to a centre moves each value by at most
    // w/2, a monotone map commutes with sorting so each order statistic moves
    // by at most w/2, and Ln is a convex combination of two of them. It is NOT
    // a measured maximum -- the worst observed residual is printed beside it.
    //
    // The 1e-12 added to the bound is comparison round-off, not slack: the
    // "two-valued astride a bin edge" shape sits AT w/2 by construction, so an
    // exact-equality bound would fail on the last bit. Same reasoning as
    // test_leq.cpp:69-74.
    const double bound = 0.5 * LevelHistogram::kBinWidthDb;
    double worst = 0.0;
    std::string worstWhere;

    for (const auto& [name, levels] : distributions()) {
        LevelHistogram h(kBase);
        for (double v : levels) h.add(v);
        REQUIRE(h.total() == levels.size());
        REQUIRE(h.belowSpan() == 0u);
        REQUIRE(h.aboveSpan() == 0u);

        for (double n : {1.0, 5.0, 10.0, 50.0, 90.0, 95.0, 99.0}) {
            const LnResult r = h.percentile(n);
            INFO(name << " at n = " << n);
            REQUIRE(r.absence == LnAbsence::None);
            REQUIRE(r.db.has_value());
            const double residual = std::abs(*r.db - exactPercentile(levels, n));
            if (residual > worst) {
                worst = residual;
                worstWhere = name + " n=" + std::to_string(n);
            }
            CHECK(residual <= bound + 1e-12);
        }
    }

    INFO("worst observed residual " << worst << " dB at " << worstWhere
                                    << ", against the w/2 bound " << bound);
    CHECK(worst <= bound + 1e-12);
    // Printed unconditionally, to full precision, so the margin is in the log
    // rather than only on a failure -- and so a reader can see that the worst
    // case REACHES the bound instead of sitting comfortably under it, which is
    // what makes it a theorem rather than a measured maximum.
    std::ostringstream report;
    report << std::setprecision(17) << "A2 worst observed Ln residual: " << worst
           << " dB, against the w/2 bound " << bound << " dB, at " << worstWhere;
    WARN(report.str());
}

// --- A4: one interpolation convention, two implementations ---------------

TEST_CASE("A4 the histogram uses the project's existing Ln interpolation",
          "[levelhistogram]") {
    for (const auto& [name, levels] : distributions()) {
        LevelHistogram h(kBase);
        for (double v : levels) h.add(v);
        const std::vector<double> centres = quantiseToCentres(levels, kBase);
        for (double n : {1.0, 5.0, 10.0, 50.0, 90.0, 95.0, 99.0}) {
            const LnResult r = h.percentile(n);
            REQUIRE(r.db.has_value());
            INFO(name << " at n = " << n);
            // Against percentileLevelDb on the SAME data quantised to centres:
            // the two must agree to round-off, because the histogram is only a
            // different way of holding the same multiset.
            CHECK_THAT(*r.db, WithinAbs(exactPercentile(centres, n), 1e-12));
        }
    }
}

// --- A5: absence, not clamping (SPL-R8, memory/a-placeholder-...) --------

TEST_CASE("A5 a percentile below the span is absent with a reason, never the floor",
          "[levelhistogram]") {
    // Detector::levelDb returns kLevelFloorDb = -200.0 for a zero state, so a
    // silent channel lands ENTIRELY below a span based at -20.0.
    LevelHistogram h(kBase);
    for (int i = 0; i < 600; ++i) h.add(kLevelFloorDb);

    CHECK(h.belowSpan() == 600u);
    CHECK(h.total() == 600u);

    const LnResult r = h.percentile(90.0);
    CHECK_FALSE(r.db.has_value());
    CHECK(r.absence == LnAbsence::BelowSpan);
    CHECK_FALSE(h.percentileDb(90.0).has_value());

    SECTION("above the span is its own reason") {
        LevelHistogram hi(kBase);
        for (int i = 0; i < 10; ++i) hi.add(kBase + 250.0);
        const LnResult up = hi.percentile(50.0);
        CHECK_FALSE(up.db.has_value());
        CHECK(up.absence == LnAbsence::AboveSpan);
    }

    SECTION("no data at all is a third reason, not silence") {
        LevelHistogram empty(kBase);
        const LnResult none = empty.percentile(50.0);
        CHECK_FALSE(none.db.has_value());
        CHECK(none.absence == LnAbsence::NoData);
    }

    SECTION("a partial span answers where the rank is inside it") {
        LevelHistogram mixed(kBase);
        for (int i = 0; i < 10; ++i) mixed.add(kLevelFloorDb);  // 10 below
        for (int i = 0; i < 90; ++i) mixed.add(100.0);          // 90 in-span
        // L50 sits in the in-span 90, so it answers; L95 reaches into the
        // below-span 10, so it does not.
        CHECK(mixed.percentile(50.0).db.has_value());
        const LnResult low = mixed.percentile(95.0);
        CHECK_FALSE(low.db.has_value());
        CHECK(low.absence == LnAbsence::BelowSpan);
    }
}

// --- A6: composability, which is what the histogram buys ----------------

TEST_CASE("A6 merge equals the concatenated stream, bitwise", "[levelhistogram]") {
    const auto dists = distributions();
    const std::vector<double>& a = dists[0].second;
    const std::vector<double>& b = dists[1].second;

    LevelHistogram ha(kBase), hb(kBase), hall(kBase);
    for (double v : a) {
        ha.add(v);
        hall.add(v);
    }
    for (double v : b) {
        hb.add(v);
        hall.add(v);
    }
    ha.merge(hb);

    // Bitwise, and it can be: merge is integer addition on counts, so no
    // floating-point arithmetic is involved in it at all.
    CHECK(ha.total() == hall.total());
    for (std::size_t i = 0; i < LevelHistogram::kBinCount; ++i) {
        REQUIRE(ha.count(i) == hall.count(i));
    }
    for (double n : {1.0, 10.0, 50.0, 90.0, 99.0}) {
        INFO("n = " << n);
        REQUIRE(ha.percentile(n).db.has_value());
        REQUIRE(hall.percentile(n).db.has_value());
        CHECK(*ha.percentile(n).db == *hall.percentile(n).db);
    }

    SECTION("merging across different bases is refused, not silently misaligned") {
        LevelHistogram other(kBase + 1.0);
        CHECK_THROWS_AS(ha.merge(other), std::invalid_argument);
    }
}

// --- A7: the DEFAULT span is run through the gate it feeds ---------------

TEST_CASE("A7 an uncalibrated session's own readings land inside the derived span",
          "[levelhistogram]") {
    // SplConfig::histogramBaseDb() == measure::kLevelFloorDb + referenceOffsetDb.
    // Uncalibrated that is -120.0 + 0.0. The constant is restated here rather
    // than included because app/src/measure/Levels.h is not visible to
    // rta_core; the scan below is what keeps the restatement honest.
    constexpr double kUncalibratedBase = -120.0;

    // Real uncalibrated readings in mean-square dBFS, which is what the Q1
    // scope default publishes: a full-scale sine, and programme material.
    const std::vector<double> session{0.0, -12.0, -30.0, -60.0};

    LevelHistogram h(kUncalibratedBase);
    for (double v : session) h.add(v);

    CHECK(h.belowSpan() == 0u);
    CHECK(h.aboveSpan() == 0u);
    const LnResult median = h.percentile(50.0);
    CHECK(median.absence == LnAbsence::None);
    CHECK(median.db.has_value());

    // The full-scale sine lands at bin 1200 of 2000: (0.0 - (-120.0))/0.1.
    // 60 % up the span, with 80 dB of headroom above it.
    CHECK(h.count(1200) == 1u);
    CHECK(h.count(1080) == 1u);  // -12 dBFS
    CHECK(h.count(900) == 1u);   // -30 dBFS
    CHECK(h.count(600) == 1u);   // -60 dBFS

    // Made red by hard-coding base = -20.0, which is what an earlier revision
    // of the plan shipped: two of these four readings fall below that span and
    // L50 becomes permanently absent. memory/a-default-must-be-run-through-
    // the-gate-it-feeds.md is this fixture's whole reason for existing.
    LevelHistogram wrong(-20.0);
    for (double v : session) wrong.add(v);
    CHECK(wrong.belowSpan() == 2u);
    CHECK(wrong.percentile(50.0).absence == LnAbsence::BelowSpan);
}

// --- A8: the CALIBRATED span is record 5's own number, recovered ---------

TEST_CASE("A8 with a +100 dB offset the span is [-20, +180) and 140 dB lands at bin 1600",
          "[levelhistogram]") {
    constexpr double kFloor = -120.0;    // measure::kLevelFloorDb
    constexpr double kOffset = 100.0;    // a typical calibration offset
    const double base = kFloor + kOffset;
    CHECK(base == -20.0);

    LevelHistogram h(base);
    CHECK_THAT(h.topDb(), WithinAbs(180.0, 1e-12));

    h.add(140.0);
    CHECK(h.aboveSpan() == 0u);
    CHECK(h.count(1600) == 1u);

    // The offset applies to the value AND to the base, where it cancels: the
    // bin index is offset-invariant, which is why (a) feeding un-offset levels
    // against the un-offset floor and (b) feeding SPL against the offset base
    // are the same histogram. SPL-R8 left this implicit; it is asserted here.
    LevelHistogram unoffset(kFloor);
    unoffset.add(140.0 - kOffset);
    CHECK(unoffset.count(1600) == 1u);
    CHECK_THAT(*unoffset.percentile(50.0).db + kOffset,
               WithinAbs(*h.percentile(50.0).db, 1e-12));
}

TEST_CASE("A9 percentile refuses an n outside [0,100] rather than guessing",
          "[levelhistogram]") {
    LevelHistogram h(kBase);
    h.add(100.0);
    CHECK_THROWS_AS(h.percentile(-0.1), std::invalid_argument);
    CHECK_THROWS_AS(h.percentile(100.1), std::invalid_argument);
    CHECK_NOTHROW(h.percentile(0.0));
    CHECK_NOTHROW(h.percentile(100.0));
}
