// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a Wave 1, task W1-A cases A7 and A8 (record
// docs/dsp/2026-09-16-spl-pro-l6a.md section 5; SPL-R8).
//
// ONE subject: the histogram's span is DERIVED from the calibration offset and
// never typed, and both ends of that decision are run through the gate they
// feed. A7 is the uncalibrated default; A8 is the calibrated case that
// recovers record section 5's own `[-20, +180)` number.
//
// Its own file because it is the one part of W1-A that reaches OUTSIDE core --
// it scans the two app headers that own the derivation -- and because
// test_level_histogram.cpp went past CLAUDE.md's 400-line cap once the scan
// PR #20's verifier asked for was added to it.
//
// memory/a-default-must-be-run-through-the-gate-it-feeds.md is why these two
// cases exist at all: an earlier revision of the plan hard-coded the base at
// -20.0, which makes every Ln of every out-of-the-box session permanently
// BelowSpan.
#include "rta/meter/LevelHistogram.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "support/SourceScan.h"

#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::meter;

// --- A7: the DEFAULT span is run through the gate it feeds ---------------

TEST_CASE("A7 an uncalibrated session's own readings land inside the derived span",
          "[levelhistogram]") {
    // SplConfig::histogramBaseDb() == measure::kLevelFloorDb + referenceOffsetDb.
    // Uncalibrated that is -120.0 + 0.0. The constant is restated here rather
    // than included because app/src/measure/Levels.h is not visible to
    // rta_core.
    constexpr double kUncalibratedBase = -120.0;

    // AND THE RESTATEMENT IS CHECKED, which an earlier revision of this comment
    // claimed ("the scan below is what keeps the restatement honest") while no
    // scan existed -- a dangling reference in the one place a reader would go
    // to confirm that a restated constant is honest. PR #20's verifier found
    // it. There is a scan now: it reads the two app headers through
    // RTA_REPO_ROOT, so this test goes RED if either the floor or the
    // derivation moves.
    {
        const std::string levels =
            rta::testing::readRepoFile("app/src/measure/Levels.h");
        INFO("app/src/measure/Levels.h must still put the floor at -120.0");
        CHECK(levels.find("kLevelFloorDb = -120.0") != std::string::npos);

        const std::string config =
            rta::testing::readRepoFile("app/src/measure/SplConfig.h");
        INFO("SplConfig::histogramBaseDb() must still be kLevelFloorDb + offset");
        CHECK(config.find("return kLevelFloorDb + referenceOffsetDb;") !=
              std::string::npos);
    }

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

