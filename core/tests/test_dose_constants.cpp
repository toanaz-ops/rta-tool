// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a Wave 1, task W1-D case D1f (record
// docs/dsp/2026-09-16-spl-pro-l6a.md section 7; SPL-R7).
//
// ONE subject: the exchange denominator `q` must be COMPUTED and never typed,
// and BOTH reasons for that are measured here rather than asserted elsewhere.
//
// It is its own file because PR #20's verifier turned this case from two lines
// into a measurement over both regulators' full 80..130 dB grids, which would
// have put test_dose.cpp past CLAUDE.md's 400-line hard cap. The subject is
// self-contained: nothing else in the wave is about the constant itself.
//
// WHY THE FILE EXISTS AT ALL, which is the part worth reading. SPL-R7 and an
// earlier revision of Dose.h both stated that "no numeric acceptance in this
// lane can distinguish either pair", leaving D1b's bitwise check as the only
// justification. That was FALSE -- test_dose_tables.cpp's D2a rejects both
// typed literals on 50 of 51 rows, and it shipped in the same commit as the
// sentence denying it. A negative claim about a whole lane is the one kind of
// claim nothing in a suite ever exercises, so this file exercises it.
#include "rta/meter/Dose.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

using namespace rta::meter;

namespace {

constexpr double kEightHours = 8.0 * 3600.0;

/// A settings block whose threshold cannot swallow a case about the exponent.
DoseSettings criterion(double levelDb, double seconds, double q) {
    DoseSettings s;
    s.criterionLevelDb = levelDb;
    s.criterionSeconds = seconds;
    s.q = q;
    s.thresholdDb = -1000.0;
    return s;
}

}  // namespace

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

    SECTION("the literal IS rejected on accuracy too, and here is the measurement") {
        // SPL-R7 asserted, and an earlier revision of this file repeated, that
        // no numeric acceptance in this lane could tell the typed literal from
        // the computed constant -- so the only reason to compute it was D1b's
        // bitwise 10^(Q/q) == 2.0. THAT WAS FALSE, and the counter-evidence
        // was one file away in the same commit: test_dose_tables.cpp's D2a
        // bounds each regulator's own exact duration formula at 1e-9 %, and
        // the literal misses it by three orders.
        //
        // The claim is a NEGATIVE one about the whole lane, which is the one
        // kind of claim nothing in a suite ever exercises unless somebody
        // writes it down as arithmetic. So it is written down as arithmetic
        // here, over exactly D2a's grid, and it is now the thing that goes red
        // if either constant is ever typed.
        constexpr double kD2aBoundPercent = 1e-9;  // test_dose_tables.cpp:89

        struct Regulator {
            const char* name;
            double criterionLevelDb;
            double exchangeDb;
            double typedLiteral;
            double (*exactSeconds)(double);
        };
        const Regulator regulators[] = {
            {"NIOSH 98-126 cl. 1.1.1", 85.0, 3.0, 9.9657843,
             [](double level) { return 60.0 * 480.0 / std::pow(2.0, (level - 85.0) / 3.0); }},
            {"29 CFR 1910.95 App. A Table G-16a", 90.0, 5.0, 16.6096404,
             [](double level) { return 3600.0 * 8.0 / std::pow(2.0, (level - 90.0) / 5.0); }},
        };

        for (const Regulator& r : regulators) {
            const double computed = exchangeDenominator(r.exchangeDb);
            double worstComputed = 0.0;
            double worstTyped = 0.0;
            int worstTypedLevel = 0;
            int typedRowsFailing = 0;

            for (int level = 80; level <= 130; ++level) {
                const double seconds = r.exactSeconds(static_cast<double>(level));
                DoseSettings s = criterion(r.criterionLevelDb, kEightHours, computed);
                Dose withComputed(s);
                withComputed.addBlock(static_cast<double>(level), seconds);
                s.q = r.typedLiteral;
                Dose withTyped(s);
                withTyped.addBlock(static_cast<double>(level), seconds);

                worstComputed = std::max(worstComputed, std::abs(withComputed.percent() - 100.0));
                const double typedDeviation = std::abs(withTyped.percent() - 100.0);
                if (typedDeviation > worstTyped) {
                    worstTyped = typedDeviation;
                    worstTypedLevel = level;
                }
                if (typedDeviation > kD2aBoundPercent) ++typedRowsFailing;
            }

            INFO(r.name << ": computed worst " << worstComputed << " %, typed worst "
                        << worstTyped << " % at " << worstTypedLevel << " dBA, "
                        << typedRowsFailing << " of 51 rows over D2a's bound");
            // The computed constant clears D2a by four orders...
            CHECK(worstComputed < kD2aBoundPercent);
            CHECK(worstComputed * 1000.0 < kD2aBoundPercent);
            // ...and the typed one misses it by three, on 50 of 51 rows. The
            // survivor is the criterion level itself, where the exponent is 0
            // and every value of q gives 10^0 = 1 exactly.
            CHECK(worstTyped > kD2aBoundPercent);
            CHECK(worstTyped > 1000.0 * kD2aBoundPercent);
            CHECK(typedRowsFailing == 50);

            std::ostringstream line;
            line << std::setprecision(6) << "D1f " << r.name << ": computed worst "
                 << worstComputed << " % (clears D2a's 1e-9 % by "
                 << (kD2aBoundPercent / worstComputed) << "x); typed "
                 << r.typedLiteral << " worst " << worstTyped << " % at " << worstTypedLevel
                 << " dBA (FAILS by " << (worstTyped / kD2aBoundPercent) << "x), "
                 << typedRowsFailing << " of 51 rows red";
            WARN(line.str());
        }

        // And the exactness leg, which is the one SPL-R7 kept. Both hold; the
        // rule stands on two legs, not on the weaker one alone.
        CHECK_FALSE(std::pow(10.0, 3.0 / 9.9657843) == 2.0);
        CHECK(std::pow(10.0, 3.0 / exchangeDenominator(3.0)) == 2.0);
    }
}

