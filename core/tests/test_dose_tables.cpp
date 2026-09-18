// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a Wave 1, task W1-D part two (record
// docs/dsp/2026-09-16-spl-pro-l6a.md section 7; SPL-R12).
//
// THE TWO-PART FIXTURE. Part (a) asserts each regulator's own footnote
// FORMULA, which is exact and is what the code implements. Part (b) asserts
// every PRINTED row within that row's own printed resolution -- a bound
// derived from the printing rather than chosen, which is why the SAME bound
// that tolerates the rounding also DETECTS the one arithmetic erratum in
// Table 1-1. The erratum is excluded by name and the bound is not widened for
// it (memory/a-threshold-read-off-a-grid-is-that-grids-floor.md).
//
// The rows themselves are in DoseTableFixtures.h, with their provenance.
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

/// NIOSH 98-126 cl. 1.1.1, printed page 1: T(min) = 480 / 2^((L-85)/3),
/// "where 3 = the exchange rate". In seconds.
double nioshExactSeconds(double levelDb) {
    return 60.0 * 480.0 / std::pow(2.0, (levelDb - 85.0) / 3.0);
}

/// 29 CFR 1910.95 App. A, the footnote to Table G-16a: T = 8 / 2^((L-90)/5).
/// In hours.
double oshaExactHours(double levelDb) {
    return 8.0 / std::pow(2.0, (levelDb - 90.0) / 5.0);
}

DoseSettings nioshSettings() {
    DoseSettings s;
    s.criterionLevelDb = 85.0;
    s.criterionSeconds = kEightHours;
    s.q = exchangeDenominator(3.0);
    s.thresholdDb = 80.0;
    return s;
}

DoseSettings oshaSettings() {
    DoseSettings s;
    s.criterionLevelDb = 90.0;
    s.criterionSeconds = kEightHours;
    s.q = exchangeDenominator(5.0);
    // The dose is computed over every level the table carries, so the
    // threshold must not exclude the bottom of the table itself.
    s.thresholdDb = 80.0;
    return s;
}

/// One block at `levelDb` for `seconds`, as a percentage of the criterion.
double doseOf(const DoseSettings& settings, double levelDb, double seconds) {
    Dose dose(settings);
    dose.addBlock(levelDb, seconds);
    return dose.percent();
}

/// Round half AWAY FROM ZERO, which is what both documents do. NOT the
/// round-half-to-even a naive implementation reaches for: Table 1-1's 109 dBA
/// row is exactly 112.5 s and the document prints 113, so banker's rounding
/// would classify that row as a TRUNCATION and make D2d's set wrong.
double roundHalfUp(double value) { return std::floor(value + 0.5); }

}  // namespace

// --- D2a: the FORMULA, at every 1 dB step --------------------------------

TEST_CASE("D2a each regulator's own exact duration formula gives exactly 100 percent",
          "[dose][tables]") {
    // This asserts the FORMULA -- which is exact, and is what the code
    // implements. Float tolerance, because 10^((L-L_c)/q) and 2^((L-L_c)/Q)
    // are two routes to the same number and agree to round-off, not to the
    // bit.
    const double tolerance = 1e-9;

    SECTION("NIOSH: 51 levels, 80 through 130") {
        int rows = 0;
        double worst = 0.0;
        for (int level = 80; level <= 130; ++level) {
            const double seconds = nioshExactSeconds(static_cast<double>(level));
            const double d = doseOf(nioshSettings(), static_cast<double>(level), seconds);
            INFO("L = " << level << " dBA, T_exact = " << seconds << " s");
            CHECK_THAT(d, WithinAbs(100.0, tolerance));
            worst = std::max(worst, std::abs(d - 100.0));
            ++rows;
        }
        CHECK(rows == 51);
        std::ostringstream report;
        report << std::setprecision(17) << "D2a NIOSH worst |D-100| = " << worst << " %";
        WARN(report.str());
    }

    SECTION("OSHA: 51 levels, 80 through 130") {
        int rows = 0;
        double worst = 0.0;
        for (int level = 80; level <= 130; ++level) {
            const double seconds = oshaExactHours(static_cast<double>(level)) * 3600.0;
            const double d = doseOf(oshaSettings(), static_cast<double>(level), seconds);
            INFO("L = " << level << " dBA, T_exact = " << seconds << " s");
            CHECK_THAT(d, WithinAbs(100.0, tolerance));
            worst = std::max(worst, std::abs(d - 100.0));
            ++rows;
        }
        CHECK(rows == 51);
        std::ostringstream report;
        report << std::setprecision(17) << "D2a OSHA worst |D-100| = " << worst << " %";
        WARN(report.str());
    }
}

// --- D2b / D2c: the PRINTED rows, within their own printing ---------------

TEST_CASE("D2b every printed NIOSH Table 1-1 row lands within its own resolution -- "
          "except the 99 dBA erratum",
          "[dose][tables]") {
    REQUIRE(kNioshTable11.size() == 50);

    std::ostringstream lines;
    lines << std::setprecision(6);
    int checked = 0;
    int excluded = 0;
    double worstFraction = 0.0;

    for (const Table11Row& row : kNioshTable11) {
        const double level = static_cast<double>(row.levelDb);
        const double printed = table11PrintedSeconds(row);
        const double exact = nioshExactSeconds(level);
        const double resolution = table11ResolutionSeconds(row);
        const double bound = 100.0 * resolution / exact;
        const double deviation = std::abs(doseOf(nioshSettings(), level, printed) - 100.0);

        lines << "  " << row.levelDb << " dBA printed " << printed << " s, exact " << exact
              << " s, |D-100| " << deviation << " %, bound " << bound << " %\n";

        if (row.levelDb == 99) {
            // D2c -- THE KNOWN ERRATUM, EXCLUDED BY NAME.
            //
            // Table 1-1 prints 18 min 59 sec = 1139 s where its own cl. 1.1.1
            // formula gives 480/2^(14/3) min = 18 min 53.93 sec = 1133.929 s.
            // The dose that follows is 0.4472 % off against a bound of
            // 0.0882 %, so the row fails -- and that is the fixture WORKING.
            // The same bound that tolerates the rounding everywhere else is
            // what detects the typo here. It is NOT widened for it: a bound
            // widened to swallow a printing error stops carrying its
            // justification from outside the grid it was measured on.
            ++excluded;
            CHECK_THAT(printed, WithinAbs(1139.0, 1e-12));
            CHECK_THAT(exact, WithinAbs(1133.929, 1e-3));
            CHECK_THAT(deviation, WithinAbs(0.4472, 1e-3));
            CHECK_THAT(bound, WithinAbs(0.0882, 1e-3));
            CHECK(deviation > bound);
            continue;
        }

        INFO("L = " << row.levelDb << " dBA, printed " << printed << " s, exact " << exact
                    << " s, bound " << bound << " %");
        CHECK(deviation <= bound);
        worstFraction = std::max(worstFraction, deviation / bound);
        ++checked;
    }

    CHECK(checked == 49);
    CHECK(excluded == 1);
    // The bound is derived from the printing, so it varies enormously across
    // the table: a duration printed to the nearest second carries no useful
    // precision once it is under two seconds. Record section 7's own figures.
    CHECK_THAT(100.0 * 60.0 / nioshExactSeconds(80.0), WithinAbs(0.0656, 1e-4));
    CHECK_THAT(100.0 * 1.0 / nioshExactSeconds(100.0), WithinAbs(0.1111, 1e-4));
    CHECK_THAT(100.0 * 1.0 / nioshExactSeconds(120.0), WithinAbs(11.2882, 1e-4));
    CHECK_THAT(100.0 * 1.0 / nioshExactSeconds(129.0), WithinAbs(90.3055, 1e-4));

    std::ostringstream report;
    report << "D2b Table 1-1: 49 rows within their own printed resolution, worst using "
           << std::setprecision(6) << (100.0 * worstFraction)
           << " % of its own bound; 99 dBA excluded as a named erratum.\n"
           << lines.str();
    WARN(report.str());
}

TEST_CASE("D2b2 every printed OSHA Table G-16a row lands within its own resolution",
          "[dose][tables]") {
    REQUIRE(kOshaTableG16a.size() == 51);

    std::ostringstream lines;
    lines << std::setprecision(6);
    double worstFraction = 0.0;
    int tightestLevel = 0;

    for (const G16aRow& row : kOshaTableG16a) {
        const double level = static_cast<double>(row.levelDb);
        const double printedHours = g16aPrintedHours(row);
        const double resolution = g16aResolutionHours(row);
        const double exact = oshaExactHours(level);
        const double bound = 100.0 * resolution / exact;
        const double deviation =
            std::abs(doseOf(oshaSettings(), level, printedHours * 3600.0) - 100.0);

        lines << "  " << row.levelDb << " dBA printed " << row.printedHours << " h (r="
              << resolution << "), exact " << exact << " h, |D-100| " << deviation
              << " %, bound " << bound << " %\n";

        INFO("L = " << row.levelDb << " dBA, printed " << row.printedHours
                    << " h, resolution " << resolution << " h, bound " << bound << " %");
        CHECK(deviation <= bound);
        if (deviation / bound > worstFraction) {
            worstFraction = deviation / bound;
            tightestLevel = row.levelDb;
        }
    }

    // ALL 51 rows hold. OSHA and NIOSH are the same shape, not mirror images:
    // both print an exact formula and a table rounded off it, and G-16a's
    // 81 dBA row is the tightest -- exact 27.857618 h printed as 27.9.
    CHECK_THAT(oshaExactHours(81.0), WithinAbs(27.857618, 1e-6));
    std::ostringstream report;
    report << "D2b2 Table G-16a: all 51 rows hold; tightest at " << tightestLevel
           << " dBA using " << std::setprecision(6) << (100.0 * worstFraction)
           << " % of its own bound.\n"
           << lines.str();
    WARN(report.str());
}

// --- D2d: the truncated rows, and the set is asserted --------------------

TEST_CASE("D2d exactly two Table 1-1 rows truncate rather than round", "[dose][tables]") {
    std::vector<int> notRounded;
    for (const Table11Row& row : kNioshTable11) {
        const double exact = nioshExactSeconds(static_cast<double>(row.levelDb));
        // The smallest unit the row actually prints: seconds if the Seconds
        // cell carries a number, minutes otherwise.
        const double unit = row.seconds == kDash ? 60.0 : 1.0;
        const double nearest = roundHalfUp(exact / unit) * unit;
        if (std::abs(table11PrintedSeconds(row) - nearest) > 1e-9) {
            notRounded.push_back(row.levelDb);
        }
    }

    // {99, 124, 127}: 99 is the arithmetic erratum D2c names, and 124 and 127
    // are floors. The document has no footnote and never says the values are
    // rounded, so the rounding convention is read off the numbers -- and it is
    // not even internally consistent. Asserted so a later "fix" to this
    // fixture goes red.
    REQUIRE(notRounded.size() == 3);
    CHECK(notRounded[0] == 99);
    CHECK(notRounded[1] == 124);
    CHECK(notRounded[2] == 127);

    // 124 dBA: exact 3.515625 s, printed 3 -- floor, where nearest is 4.
    CHECK_THAT(nioshExactSeconds(124.0), WithinAbs(3.515625, 1e-9));
    CHECK(std::floor(nioshExactSeconds(124.0)) == 3.0);
    CHECK(roundHalfUp(nioshExactSeconds(124.0)) == 4.0);
    // 127 dBA: exact 1.7578125 s, printed 1 -- floor, where nearest is 2.
    CHECK_THAT(nioshExactSeconds(127.0), WithinAbs(1.7578125, 1e-9));
    CHECK(std::floor(nioshExactSeconds(127.0)) == 1.0);
    CHECK(roundHalfUp(nioshExactSeconds(127.0)) == 2.0);

    SECTION("109 dBA is an exact half and the document rounds it UP, not to even") {
        // 480/2^8 min = 1.875 min = 112.5 s exactly, printed 1 min 53 sec.
        // A fixture written with round-half-to-even would call this a
        // truncation and make the set above four rows long instead of three.
        CHECK(nioshExactSeconds(109.0) == 112.5);
        CHECK(table11PrintedSeconds(kNioshTable11[29]) == 113.0);
        CHECK(kNioshTable11[29].levelDb == 109);
        CHECK(roundHalfUp(112.5) == 113.0);
    }

    SECTION("the 51st printed row is a RANGE and an inequality, not a duration") {
        // "130-140 -> < 1 sec". It cannot be a (level, duration) pair, so it
        // is not in the array; what it asserts is that the exact duration at
        // both ends of the range is under one second.
        CHECK(nioshExactSeconds(130.0) < 1.0);
        CHECK(nioshExactSeconds(140.0) < 1.0);
        CHECK(nioshExactSeconds(129.0) > 1.0);
    }
}

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
