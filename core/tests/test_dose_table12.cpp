// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a Wave 1, task W1-D cases D2e and D2f (record
// docs/dsp/2026-09-16-spl-pro-l6a.md section 7).
//
// NIOSH 98-126 Table 1-2 is a DIFFERENT table asking a different question from
// the duration tables next door: it converts a dose percentage to an 8-hour
// TWA, and its own printed footnote is `*TWA = 10 x Log(D/100) + 85`. That
// footnote is `q = 10` exactly -- while Table 1-1, three pages earlier in the
// same chapter, needs `q = 3/log10(2)`.
//
// That is record section 7's central finding, and this file asserts it in BOTH
// directions rather than asserting about it: Table 1-2's last row pins q = 10,
// and Table 1-1's own printed rows REJECT q = 10. One chapter, one document,
// two exchange constants, up to 4.2549 % of dose apart at 140 dB(A). Which one
// ships is still the owner's call (record section 13 Q4).
//
// Split from test_dose_tables.cpp at the 400-line cap; the seam is the table.
#include "rta/meter/Dose.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "DoseTableFixtures.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::meter;
using namespace rta::testing;

namespace {

constexpr double kEightHours = 8.0 * 3600.0;

/// NIOSH 98-126 cl. 1.1.1, printed p. 1: T(min) = 480 / 2^((L-85)/3). Seconds.
double nioshExactSeconds(double levelDb) {
    return 60.0 * 480.0 / std::pow(2.0, (levelDb - 85.0) / 3.0);
}

DoseSettings nioshSettings() {
    DoseSettings s;
    s.criterionLevelDb = 85.0;
    s.criterionSeconds = kEightHours;
    s.q = exchangeDenominator(3.0);
    s.thresholdDb = 80.0;
    return s;
}

double doseOf(const DoseSettings& settings, double levelDb, double seconds) {
    Dose dose(settings);
    dose.addBlock(levelDb, seconds);
    return dose.percent();
}

}  // namespace

// --- D2e / D2f: Table 1-2 pins q = 10, in both directions ----------------

TEST_CASE("D2e Table 1-2 pins q = 10 while Table 1-1 rejects it", "[dose][tables]") {
    // The record's central section 7 finding, asserted rather than
    // asserted-about: ONE chapter of ONE document contains two tables that
    // need two different exchange constants.
    //
    // Table 1-2's last row is the proof and needs no interpretation:
    // 32,500,000 % -> 140.1 dBA, and 10*log10(325000) + 85 = 140.1188.
    const double atQten = 10.0 * std::log10(325000.0) + 85.0;
    CHECK_THAT(atQten, WithinAbs(140.118834, 1e-6));
    CHECK_THAT(std::round(atQten * 10.0) / 10.0, WithinAbs(140.1, 1e-12));
    // At the exchange-rate constant the same row would print 139.9.
    const double atQexchange = exchangeDenominator(3.0) * std::log10(325000.0) + 85.0;
    CHECK_THAT(atQexchange, WithinAbs(139.930241, 1e-6));
    CHECK(std::abs(atQexchange - 140.1) > 0.15);

    SECTION("and Table 1-1's own rows REJECT q = 10") {
        DoseSettings energy = nioshSettings();
        energy.q = 10.0;
        // 80 dBA: 0.4023 % against a 0.0656 % bound. 100 dBA: 1.1788 %
        // against 0.1111 %. The bound derived from the printing is tight
        // enough to tell the two constants apart, which is what makes the
        // inconsistency a measurement and not a reading of the prose.
        for (int level : {80, 100}) {
            const Table11Row& row = kNioshTable11[static_cast<std::size_t>(level - 80)];
            REQUIRE(row.levelDb == level);
            const double printed = table11PrintedSeconds(row);
            const double bound =
                100.0 * table11ResolutionSeconds(row) / nioshExactSeconds(level);
            const double deviation =
                std::abs(doseOf(energy, static_cast<double>(level), printed) - 100.0);
            INFO("L = " << level << ", q = 10 deviation " << deviation << " % vs bound "
                        << bound << " %");
            CHECK(deviation > bound);
        }
    }
}

TEST_CASE("D2f Table 1-2's own two errata are named, and every other row is within "
          "0.05 dB",
          "[dose][tables]") {
    REQUIRE(kNioshTable12.size() == 121);

    std::vector<long long> beyond;
    double worst = 0.0;
    for (const Table12Row& row : kNioshTable12) {
        // The table's own printed footnote: *TWA = 10 x Log(D/100) + 85.
        const double exact =
            10.0 * std::log10(static_cast<double>(row.dosePercent) / 100.0) + 85.0;
        const double deviation = std::abs(row.printedTwaDb - exact);
        if (deviation > 0.05) {
            beyond.push_back(row.dosePercent);
        } else {
            worst = std::max(worst, deviation);
        }
    }

    REQUIRE(beyond.size() == 2);
    CHECK(beyond[0] == 50000);
    CHECK(beyond[1] == 26000000);

    // 50,000 % prints 102.0 where the formula gives 111.9897 -- a single-digit
    // substitution in the tens place (112.0 -> 102.0), not a transposition.
    // The neighbours BRACKET it, so the printed value also breaks the table's
    // own monotonicity, which is the independent argument that it is an
    // erratum and not a different convention.
    CHECK_THAT(10.0 * std::log10(500.0) + 85.0, WithinAbs(111.9897, 1e-4));
    CHECK_THAT(10.0 * std::log10(450.0) + 85.0, WithinAbs(111.5321, 1e-4));
    CHECK_THAT(10.0 * std::log10(600.0) + 85.0, WithinAbs(112.7815, 1e-4));
    CHECK(102.0 < 111.5);  // 45,000 -> 111.5 precedes it
    CHECK(102.0 < 112.8);  // 60,000 -> 112.8 follows it

    // 26,000,000 % prints 139.0 where the formula gives 139.1497.
    CHECK_THAT(10.0 * std::log10(260000.0) + 85.0, WithinAbs(139.1497, 1e-4));

    // Every OTHER row of the 121 lands within 0.05 dB, which is what makes the
    // two above errata rather than a rounding convention.
    CHECK(worst <= 0.05);
    std::ostringstream report;
    report << std::setprecision(17)
           << "D2f Table 1-2: 119 of 121 rows within " << worst
           << " dB of 10log10(D/100)+85; 50,000 % and 26,000,000 % excluded by name";
    WARN(report.str());
}
