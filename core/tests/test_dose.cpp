// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a Wave 1, task W1-D parts one and three (record
// docs/dsp/2026-09-16-spl-pro-l6a.md sections 7 and 7a).
//
// The IDENTITIES, which are exact for every q, and the STRUCTURAL checks. The
// regulators' printed tables are in test_dose_tables.cpp -- split before
// either file was written, because one file carrying both would have been past
// CLAUDE.md's 400-line cap on data alone.
#include "rta/meter/Dose.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "support/SourceScan.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <type_traits>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using namespace rta::meter;
using rta::testing::readRepoFile;
using rta::testing::repoFileCode;

namespace {

/// A settings block with the criterion at L_c for T_c and a threshold far
/// below anything a test feeds it, so the threshold cannot silently swallow a
/// case that is about the exponent.
DoseSettings criterion(double levelDb, double seconds, double q) {
    DoseSettings s;
    s.criterionLevelDb = levelDb;
    s.criterionSeconds = seconds;
    s.q = q;
    s.thresholdDb = -1000.0;
    return s;
}

template <class... Args>
concept ExposureCallable = requires(Args... args) { exposureLevelDb(args...); };

constexpr double kEightHours = 8.0 * 3600.0;

}  // namespace

// --- D1a..D1c: the exchange identities, exact -----------------------------

TEST_CASE("D1a the criterion level for the criterion time is exactly 100 percent",
          "[dose]") {
    // For EVERY q: at L = L_c the exponent is zero, 10^0 is exactly 1.0, and
    // the accumulated seconds are exactly T_c.
    for (double q : {10.0, exchangeDenominator(3.0), exchangeDenominator(5.0),
                     exchangeDenominator(4.0)}) {
        INFO("q = " << q);
        Dose dose(criterion(85.0, kEightHours, q));
        dose.addBlock(85.0, kEightHours);
        CHECK(dose.percent() == 100.0);
        REQUIRE(dose.twaDb().has_value());
        CHECK_THAT(*dose.twaDb(), WithinAbs(85.0, 1e-12));
    }
}

TEST_CASE("D1b one exchange UP for half the time is exactly 100 percent, bitwise",
          "[dose]") {
    // 10^(Q/q) with q = Q/log10(2) is 10^(Q*log10(2)/Q) = 10^log10(2) = 2.
    // MEASURED bitwise on this toolchain rather than assumed -- see the NOTE
    // in the section below.
    for (double exchangeDb : {3.0, 4.0, 5.0, 6.0}) {
        const double q = exchangeDenominator(exchangeDb);
        INFO("Q = " << exchangeDb << " dB, q = " << q);
        Dose dose(criterion(85.0, kEightHours, q));
        dose.addBlock(85.0 + exchangeDb, kEightHours / 2.0);
        CHECK(dose.percent() == 100.0);
    }
}

TEST_CASE("D1c one exchange DOWN for twice the time is exactly 100 percent, bitwise",
          "[dose]") {
    for (double exchangeDb : {3.0, 4.0, 5.0, 6.0}) {
        const double q = exchangeDenominator(exchangeDb);
        INFO("Q = " << exchangeDb << " dB, q = " << q);
        Dose dose(criterion(85.0, kEightHours, q));
        dose.addBlock(85.0 - exchangeDb, 2.0 * kEightHours);
        CHECK(dose.percent() == 100.0);
    }
}

TEST_CASE("D1b/D1c are bitwise because the arithmetic MEASURES as bitwise", "[dose]") {
    // NOT an assumption. An earlier revision of the plan loosened D1b and D1c
    // to 1e-12 on the asserted grounds that pow(10, log10(2)) is not
    // bit-identically 2.0. It is, here:
    CHECK(std::pow(10.0, std::log10(2.0)) == 2.0);
    for (double exchangeDb : {3.0, 4.0, 5.0, 6.0}) {
        const double q = exchangeDenominator(exchangeDb);
        INFO("Q = " << exchangeDb);
        CHECK(std::pow(10.0, exchangeDb / q) == 2.0);
        CHECK(std::pow(10.0, -exchangeDb / q) == 0.5);
    }
    // NOTE FOR A FAILING CI JOB, and it is deliberate that this is a note and
    // not a tolerance: these run on three toolchains. If one of them makes any
    // comparison in this case fail, that is a FINDING -- a named libm on a
    // named operating system, reported in the pull request body -- and NOT a
    // tolerance to widen in silence. Giving up an available bitwise check on
    // an unmeasured negative is the inverse of this project's own rule that
    // the tolerance follows the algorithm.
}

// --- D1d: the asymmetry an average would hide ----------------------------

TEST_CASE("D1d half an exchange under and half over is 125 percent, not 100", "[dose]") {
    // The arithmetic mean of (L_c - Q) and (L_c + Q) is L_c, so anything that
    // averaged LEVELS would report 100 %. The dose is an average of ENERGY-
    // like terms: 0.25 + 1.0 = 1.25.
    for (double exchangeDb : {3.0, 5.0}) {
        const double q = exchangeDenominator(exchangeDb);
        INFO("Q = " << exchangeDb);
        Dose dose(criterion(85.0, kEightHours, q));
        dose.addBlock(85.0 - exchangeDb, kEightHours / 2.0);
        dose.addBlock(85.0 + exchangeDb, kEightHours / 2.0);
        CHECK_THAT(dose.percent(), WithinAbs(125.0, 1e-12));
    }
}

// --- D1e: the threshold contributes EXACTLY zero -------------------------

TEST_CASE("D1e below the threshold contributes exactly zero, and the time is reported",
          "[dose]") {
    DoseSettings s;
    s.criterionLevelDb = 85.0;
    s.criterionSeconds = kEightHours;
    s.q = exchangeDenominator(3.0);
    s.thresholdDb = 80.0;

    Dose dose(s);
    for (int i = 0; i < 3600; ++i) dose.addBlock(79.999, 1.0);

    // Bitwise zero. A block below the threshold must not add a small number:
    // 3600 s at 79.999 dBA would otherwise contribute about 1.7 % of a dose
    // the regulator says is not part of the integral at all.
    CHECK(dose.percent() == 0.0);
    CHECK(dose.secondsBelowThreshold() == 3600.0);
    CHECK(dose.elapsedSeconds() == 3600.0);
    CHECK_FALSE(dose.twaDb().has_value());

    SECTION("AT the threshold it contributes -- 80 is inside NIOSH's own range") {
        // cl. 1.3.3: "all ... sound levels from 80 to 140 dBA shall be
        // integrated", which is why Table 1-1 starts at 80 and not at 85.
        Dose atThreshold(s);
        atThreshold.addBlock(80.0, 3600.0);
        CHECK(atThreshold.percent() > 0.0);
        CHECK(atThreshold.secondsBelowThreshold() == 0.0);
    }

    SECTION("the time below threshold is never folded in") {
        Dose mixed(s);
        mixed.addBlock(70.0, 3600.0);  // below: zero dose, one hour of time
        mixed.addBlock(85.0, kEightHours);
        CHECK(mixed.percent() == 100.0);
        CHECK(mixed.secondsBelowThreshold() == 3600.0);
        CHECK(mixed.elapsedSeconds() == 3600.0 + kEightHours);
    }
}

// --- D1f: the constant is COMPUTED, not typed (SPL-R7) -------------------

TEST_CASE("D1f the exchange denominators are computed, and the readable decimals differ",
          "[dose]") {
    CHECK(exchangeDenominator(3.0) == 3.0 / std::log10(2.0));
    CHECK(exchangeDenominator(5.0) == 5.0 / std::log10(2.0));

    // The record prints these as readable decimals. Asserting the computed
    // value against the printed one FAILS, and this case says so in its own
    // name rather than leaving a reader to wonder whether they are the same.
    CHECK_FALSE(exchangeDenominator(3.0) == 9.9657843);
    CHECK_FALSE(exchangeDenominator(5.0) == 16.6096404);

    std::ostringstream report;
    report << std::setprecision(17) << "D1f 3/log10(2) = " << exchangeDenominator(3.0)
           << " vs the printed 9.9657843 (delta "
           << (9.9657843 - exchangeDenominator(3.0)) << "); 5/log10(2) = "
           << exchangeDenominator(5.0) << " vs the printed 16.6096404 (delta "
           << (16.6096404 - exchangeDenominator(5.0)) << ")";
    WARN(report.str());

    SECTION("and the literal is NOT rejected on accuracy -- that reason was refuted") {
        // SPL-R7's original justification claimed the literal fails a dose
        // bound. It does not, by five to eight orders. The reason to compute
        // it is that 10^(Q/q) is then exactly 2.0 (D1b/D1c above), which the
        // literal does not give.
        const double literal = 9.9657843;
        const double computed = exchangeDenominator(3.0);
        CHECK(std::abs(literal - computed) / computed < 2e-9);
        CHECK_FALSE(std::pow(10.0, 3.0 / literal) == 2.0);
    }
}

// --- D1g: the gap between the two NIOSH conventions ----------------------

TEST_CASE("D1g q=10 against q=3/log10(2) is a closed form, not a figure", "[dose]") {
    // D(q=10)/D(q=3/log10 2) = 10^(dL*(1/10 - log10(2)/3)).
    const double coefficient = 1.0 / 10.0 - std::log10(2.0) / 3.0;
    INFO("exponent coefficient " << coefficient);
    CHECK_THAT(coefficient, WithinAbs(-0.00034333, 1e-8));

    struct Expect {
        double deltaLevel;
        double percentLow;
    };
    // Record section 7's own table, recomputed here from the closed form and
    // checked against its printed figures.
    const Expect expected[] = {{15.0, 1.1788}, {30.0, 2.3437}, {45.0, 3.4949},
                               {55.0, 4.2549}};
    for (const Expect& e : expected) {
        const double level = 85.0 + e.deltaLevel;
        Dose energy(criterion(85.0, kEightHours, 10.0));
        Dose exchange(criterion(85.0, kEightHours, exchangeDenominator(3.0)));
        energy.addBlock(level, 1.0);
        exchange.addBlock(level, 1.0);

        const double ratio = energy.percent() / exchange.percent();
        INFO("dL = " << e.deltaLevel << " dB, ratio " << ratio);
        CHECK_THAT(ratio, WithinRel(std::pow(10.0, e.deltaLevel * coefficient), 1e-9));
        CHECK_THAT(100.0 * (1.0 - ratio), WithinAbs(e.percentLow, 1e-4));
    }

    SECTION("at dL = 30 the same fact is 2^10/10^3 exactly") {
        Dose energy(criterion(85.0, kEightHours, 10.0));
        Dose exchange(criterion(85.0, kEightHours, exchangeDenominator(3.0)));
        energy.addBlock(115.0, 1.0);
        exchange.addBlock(115.0, 1.0);
        // 10^(30/10) = 1000 and 2^(30/3) = 1024, so the exchange convention
        // reads 1.024x higher. Nothing about this needs a tolerance argument.
        CHECK_THAT(exchange.percent() / energy.percent(),
                   WithinRel(1024.0 / 1000.0, 1e-12));
    }
}

// --- D1h: L_EX,8h is ENERGY and never takes q ----------------------------

TEST_CASE("D1h exposureLevelDb is Leq + 10log10(T/8h) and has no q", "[dose]") {
    for (double seconds : {kEightHours, kEightHours / 2.0, 3600.0, 60.0, 4.0 * kEightHours}) {
        INFO("T = " << seconds << " s");
        CHECK_THAT(exposureLevelDb(94.0, seconds),
                   WithinAbs(94.0 + 10.0 * std::log10(seconds / kEightHours), 1e-12));
    }
    // 8 h at the level IS the level; half the time is 3.0103 dB below it.
    CHECK(exposureLevelDb(85.0, kEightHours) == 85.0);
    CHECK_THAT(exposureLevelDb(85.0, kEightHours / 2.0),
               WithinAbs(85.0 - 10.0 * std::log10(2.0), 1e-12));

    // Structural: there is no three-argument form. A q here would silently
    // turn an energy average into an exchange-rate average, and record
    // section 7's rule is that L_EX,8h never takes one.
    static_assert(ExposureCallable<double, double>, "the two-argument form exists");
    static_assert(!ExposureCallable<double, double, double>,
                  "exposureLevelDb must not take a third parameter -- L_EX,8h is energy");
}

// --- D2g: two accumulators, one block stream -----------------------------

TEST_CASE("D2g two accumulators run at once with different thresholds", "[dose]") {
    // Larson Davis NUM_SLM_DOSES = 2; Smaart SPL's "Exposure O" and
    // "Exposure N" columns side by side in one log. The names are in this TEST
    // and never in core -- D3b asserts that.
    DoseSettings niosh;
    niosh.criterionLevelDb = 85.0;
    niosh.criterionSeconds = kEightHours;
    niosh.q = exchangeDenominator(3.0);
    niosh.thresholdDb = 80.0;

    DoseSettings osha;
    osha.criterionLevelDb = 90.0;
    osha.criterionSeconds = kEightHours;
    osha.q = exchangeDenominator(5.0);
    osha.thresholdDb = 90.0;

    Dose a(niosh);
    Dose b(osha);
    // ONE stream of blocks, both accumulators fed from it.
    for (int i = 0; i < 3600; ++i) {
        a.addBlock(85.0, 1.0);
        b.addBlock(85.0, 1.0);
    }

    // 85 dBA is above the 80 threshold and below the 90 one, so it contributes
    // to exactly one of the two -- and EXACTLY zero to the other.
    CHECK_THAT(a.percent(), WithinAbs(100.0 * 3600.0 / kEightHours, 1e-12));
    CHECK(b.percent() == 0.0);
    CHECK(b.secondsBelowThreshold() == 3600.0);
    // Both have seen the same elapsed time; only the integral differs.
    CHECK(a.elapsedSeconds() == b.elapsedSeconds());

    SECTION("above both thresholds they disagree by their exchange rates alone") {
        Dose c(niosh);
        Dose d(osha);
        c.addBlock(100.0, 3600.0);
        d.addBlock(100.0, 3600.0);
        // 100 dBA: 5 exchanges over 85 at 3 dB, 2 over 90 at 5 dB.
        CHECK_THAT(c.percent(), WithinRel(100.0 * (3600.0 / kEightHours) * 32.0, 1e-9));
        CHECK_THAT(d.percent(), WithinRel(100.0 * (3600.0 / kEightHours) * 4.0, 1e-9));
    }
}

// --- D1: the projection, and its absence ---------------------------------

TEST_CASE("D1i projected dose is D_now * T_c/T_elapsed, and absent before anything "
          "elapsed",
          "[dose]") {
    Dose dose(criterion(85.0, kEightHours, exchangeDenominator(3.0)));
    CHECK_FALSE(dose.projectedPercent().has_value());
    CHECK(dose.elapsedSeconds() == 0.0);

    dose.addBlock(85.0, 3600.0);
    REQUIRE(dose.projectedPercent().has_value());
    // One hour at the criterion level is 12.5 % of an 8 h dose, and projects
    // to 100 % if it keeps up.
    CHECK_THAT(dose.percent(), WithinAbs(12.5, 1e-12));
    CHECK_THAT(*dose.projectedPercent(), WithinAbs(100.0, 1e-12));

    SECTION("reset puts both back, and the projection back to absent") {
        dose.reset();
        CHECK(dose.percent() == 0.0);
        CHECK(dose.elapsedSeconds() == 0.0);
        CHECK_FALSE(dose.projectedPercent().has_value());
    }
}

// --- D3: structural ------------------------------------------------------

TEST_CASE("D3a peak never enters the dose integral", "[dose]") {
    // Record section 7a. The three "140" ceilings are three different
    // measurements -- an OSHA peak limit that names no weighting and applies
    // only to impulsive or impact noise, an EU dB(C) peak, and a NIOSH dBA
    // LEVEL ceiling -- so a dose that swallowed a peak could not tell them
    // apart. There is nothing in these two files to swallow it with.
    //
    // Scanned as CODE, with line comments stripped: the header MUST be able to
    // say the word "peak" in order to record that peak is deliberately absent.
    // See support/SourceScan.h for why that distinction is the whole point.
    for (const char* file : {"core/include/rta/meter/Dose.h", "core/src/meter/Dose.cpp"}) {
        const std::string code = repoFileCode(file);
        INFO(file);
        REQUIRE(code.find("namespace rta::meter") != std::string::npos);
        CHECK(code.find("peak") == std::string::npos);
    }

    SECTION("and the absence IS documented, so it cannot be dropped quietly") {
        const std::string header =
            rta::testing::lowered(readRepoFile("core/include/rta/meter/Dose.h"));
        CHECK(header.find("peak never enters this class") != std::string::npos);
    }
}

TEST_CASE("D3b no regulator's name appears in core CODE", "[dose]") {
    // The presets are values assembled in app/, so a change of regulation is a
    // change of data and not a change of core. Clause CITATIONS stay -- the
    // verification standard asks for them by name -- which is exactly why this
    // scans code and not prose.
    for (const char* file : {"core/include/rta/meter/Dose.h", "core/src/meter/Dose.cpp"}) {
        const std::string code = repoFileCode(file);
        INFO(file);
        // Two sentinels present in BOTH files, so a scan that silently read
        // nothing cannot pass. The first version used "dosesettings", which
        // is in the header and NOT in the .cpp -- the vacuity guard caught
        // itself, which is the only way to find out that it works.
        REQUIRE(code.find("namespace rta::meter") != std::string::npos);
        REQUIRE(code.find("dose") != std::string::npos);
        for (const char* forbidden : {"niosh", "osha", "vlarem", "ansi"}) {
            INFO("must not use " << forbidden << " in CODE");
            CHECK(code.find(forbidden) == std::string::npos);
        }
    }

    SECTION("the clause citations ARE in the prose, which is the other half") {
        const std::string header = readRepoFile("core/include/rta/meter/Dose.h");
        CHECK(header.find("IEC 61252") != std::string::npos);
        CHECK(header.find("NIOSH 98-126") != std::string::npos);
        CHECK(header.find("OSHA App. A I(2)") != std::string::npos);
    }
}
