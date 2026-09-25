// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a Wave 1, task W1-E (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md section 7a).
//
// Three frameworks print "140" and none of the three means the same thing by
// it. This file asserts that the shipped table keeps them apart, and that the
// app has no way to print a bare "peak".
#include "measure/SplCriteria.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "measure/SplConfig.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::measure;

namespace {

std::string lowered(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

/// Every .h/.cpp under a directory of this repository, recursively.
std::vector<std::filesystem::path> sourcesUnder(const char* relativeDir) {
    const std::filesystem::path root = std::filesystem::path(RTA_REPO_ROOT) / relativeDir;
    std::vector<std::filesystem::path> out;
    if (!std::filesystem::exists(root)) return out;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const std::string ext = entry.path().extension().string();
        if (ext == ".h" || ext == ".cpp") out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// A string literal's contents, one per element, from a source file. A readout
/// LABEL is a string literal, so this is what the E2 scan looks at -- comments
/// and identifiers are not what an operator reads off the screen.
std::vector<std::string> stringLiterals(const std::string& text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    bool inLineComment = false;
    while (i < text.size()) {
        if (inLineComment) {
            if (text[i] == '\n') inLineComment = false;
            ++i;
            continue;
        }
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            inLineComment = true;
            i += 2;
            continue;
        }
        if (text[i] == '"') {
            std::string literal;
            ++i;
            while (i < text.size() && text[i] != '"') {
                if (text[i] == '\\' && i + 1 < text.size()) {
                    literal.push_back(text[i]);
                    ++i;
                }
                literal.push_back(text[i]);
                ++i;
            }
            out.push_back(literal);
            ++i;
            continue;
        }
        ++i;
    }
    return out;
}

}  // namespace

// --- E1: three criteria, three different quantities ----------------------

TEST_CASE("E1 the three 140s are three criteria and cannot be collapsed",
          "[splcriteria]") {
    REQUIRE(kPeakAndCeilingCriteria.size() == 3);

    // Each row carries the same NUMBER and a different meaning. That is the
    // whole point: a table keyed on the number would have one row.
    for (const SplCriterion& c : kPeakAndCeilingCriteria) {
        INFO(c.framework);
        CHECK_THAT(c.limitDb, WithinAbs(140.0, 1e-12));
        CHECK_FALSE(c.framework.empty());
        CHECK_FALSE(c.citation.empty());
        CHECK_FALSE(c.readoutLabel.empty());
        CHECK(labelNamesItsQuantity(c.readoutLabel));
    }

    SECTION("OSHA: a PEAK limit, NO weighting named, impulsive or impact ONLY") {
        const SplCriterion& c = kPeakAndCeilingCriteria[0];
        CHECK(c.framework == "OSHA");
        CHECK(c.quantity == CriterionQuantity::PeakSoundPressureLevel);
        // "Specifies none" is not "specifies Z". The empty string plus the
        // explicit flag is what keeps a later reader from writing "dB(Z)"
        // into a report and citing the CFR for it.
        CHECK(c.weighting.empty());
        CHECK(c.weightingIsUnspecified);
        CHECK(c.scope == CriterionScope::ImpulsiveOrImpactOnly);
        CHECK(c.citation.find("impulsive or impact") != std::string_view::npos);
    }

    SECTION("EU 2003/10/EC: a dB(C) peak, all noise, and it has three tiers") {
        const SplCriterion& c = kPeakAndCeilingCriteria[1];
        CHECK(c.quantity == CriterionQuantity::PeakSoundPressureLevel);
        CHECK(c.weighting == "C");
        CHECK_FALSE(c.weightingIsUnspecified);
        CHECK(c.scope == CriterionScope::AllNoise);
        // The tiers exist only here, which is the independent reason this row
        // cannot be folded into either of the others.
        REQUIRE(kEuPeakTiersDb.size() == 3);
        CHECK_THAT(kEuPeakTiersDb[0], WithinAbs(140.0, 1e-12));
        CHECK_THAT(kEuPeakTiersDb[1], WithinAbs(137.0, 1e-12));
        CHECK_THAT(kEuPeakTiersDb[2], WithinAbs(135.0, 1e-12));
    }

    SECTION("NIOSH: an A-weighted SLOW level ceiling, not a peak at all") {
        const SplCriterion& c = kPeakAndCeilingCriteria[2];
        CHECK(c.quantity == CriterionQuantity::TimeWeightedLevel);
        CHECK(c.weighting == "A");
        CHECK_FALSE(c.weightingIsUnspecified);
        // cl. 1.1.4 is one sentence with no footnote, and it covers impulsive
        // noise EXPLICITLY -- which is why its scope is not the OSHA one.
        CHECK(c.scope == CriterionScope::AllNoise);
        CHECK(c.citation.find("1.1.4") != std::string_view::npos);

        // THE DETECTOR, which this row got WRONG until PR #20's verifier read
        // the primary. 98-126 cl. 1.3.3, printed p. 4, is normative -- "the
        // meter response shall be set at SLOW" -- and ch. 4, printed p. 25,
        // repeats it. So S, and the label is L_ASmax and never L_AFmax.
        CHECK(c.timeWeighting == "S");
        CHECK(c.readoutLabel == "L_ASmax");
        CHECK(c.readoutLabel.find("L_AFmax") == std::string_view::npos);
        CHECK(c.citation.find("SLOW") != std::string_view::npos);
        CHECK(c.citation.find("1.3.3") != std::string_view::npos);
    }

    SECTION("a PEAK has no exponential time weighting, and that is not the same "
            "as unspecified") {
        // The two peak rows carry an EMPTY timeWeighting because F and S do not
        // apply to an instantaneous maximum at all -- a different fact from the
        // OSHA row's empty `weighting`, which means the CFR named none.
        for (std::size_t i : {std::size_t{0}, std::size_t{1}}) {
            const SplCriterion& c = kPeakAndCeilingCriteria[i];
            INFO(c.framework);
            REQUIRE(c.quantity == CriterionQuantity::PeakSoundPressureLevel);
            CHECK(c.timeWeighting.empty());
        }
        // ...and the one level ceiling is the one row that HAS a detector.
        CHECK_FALSE(kPeakAndCeilingCriteria[2].timeWeighting.empty());
    }

    SECTION("no two rows agree on all three of quantity, weighting and scope") {
        // Made red by collapsing any two rows into one: this is the structural
        // form of "these are three measurements, not one".
        std::set<std::string> signatures;
        for (const SplCriterion& c : kPeakAndCeilingCriteria) {
            std::ostringstream key;
            key << static_cast<int>(c.quantity) << '|' << c.weighting << '|'
                << c.weightingIsUnspecified << '|' << static_cast<int>(c.scope);
            signatures.insert(key.str());
        }
        CHECK(signatures.size() == 3);
    }
}

// --- E2: the app never prints a bare "peak" ------------------------------

TEST_CASE("E2 no SPL readout label in app/src/export or app/src/view says peak "
          "without naming its quantity",
          "[splcriteria]") {
    // "Peak" IS AN OVERLOADED WORD IN THIS CODEBASE, and that is the finding
    // this case was written by. The plan's row says to grep app/src/export and
    // app/src/view for a readout label matching `peak` without an adjacent
    // qualifier. Run as written it reports FIVE offenders, and not one of them
    // is a sound pressure peak: four are EQ and FIR design vocabulary
    // ("peaking" is a filter TYPE; "peak_0dbfs", "peak_gain_db" and
    // "coefficient_peak" are FIR normalisation and coefficient quantities).
    //
    // So the check is an SPL-peak check with an EXPLICIT allow-list, and the
    // allow-list is made to expire: every entry must actually be FOUND, so a
    // stale exemption for a literal somebody renamed goes red instead of
    // quietly widening the scan. That is the same discipline the framework
    // guard's `OK (N files scanned)` count enforces -- read the number, never
    // predict it.
    struct Allowed {
        const char* literal;
        const char* why;
        int seen;
    };
    Allowed allowed[] = {
        {"peaking", "an EQ filter TYPE (rta::eq::FilterType::Peaking), not a level", 0},
        {"peak_0dbfs", "an FIR normalisation mode, not a level", 0},
        {"peak_gain_db", "an FIR design quantity: the filter's own gain peak", 0},
        {"coefficient_peak", "the largest FIR coefficient, not a sound pressure", 0},
        // W2-C (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md, record sec.10
        // item C8): the CSV/JSON column name for the sampled C-weighted peak.
        // It DOES name its quantity -- the "C" -- just in the wire format's
        // own camelCase vocabulary rather than the readout-label "L_Cpeak"
        // form kQuantityTokens matches, and C8's acceptance fixes this exact
        // spelling, so the column is exempted here rather than renamed.
        {"peakcdb", "SplLog.h's CSV/JSON column for the sampled C-weighted peak (C8)", 0},
    };

    std::size_t filesScanned = 0;
    std::size_t literalsScanned = 0;
    std::size_t peakLiterals = 0;
    std::vector<std::string> offenders;

    for (const char* dir : {"app/src/export", "app/src/view"}) {
        for (const std::filesystem::path& path : sourcesUnder(dir)) {
            ++filesScanned;
            for (const std::string& literal : stringLiterals(readFile(path))) {
                ++literalsScanned;
                const std::string lower = lowered(literal);
                if (lower.find("peak") == std::string::npos) continue;
                ++peakLiterals;

                bool exempt = false;
                for (Allowed& a : allowed) {
                    if (lower == a.literal) {
                        ++a.seen;
                        exempt = true;
                        break;
                    }
                }
                if (exempt) continue;

                // An SPL peak label must name its quantity. THE TOKEN LIST IS
                // THE HEADER'S, not a copy: this was five hand-written string
                // comparisons, and PR #20's verifier found they had already
                // drifted from `labelNamesItsQuantity` -- the copy here carried
                // `l_afmax` and omitted `l_asmax`, so correcting the NIOSH row
                // to its normative SLOW detector would have made a view file
                // printing the corrected label read as an offender. One list,
                // read twice.
                bool qualified = lower.find("sampled") != std::string::npos;
                for (std::string_view token : kQuantityTokens) {
                    if (lower.find(lowered(std::string(token))) != std::string::npos) {
                        qualified = true;
                        break;
                    }
                }
                if (!qualified) {
                    offenders.push_back(path.filename().string() + ": \"" + literal + "\"");
                }
            }
        }
    }

    // The scan has to be shown to be looking at something, or it passes
    // vacuously. These counts RISE as Waves 2 and 4a land their SPL export and
    // view files, which is when the check starts doing its real work.
    INFO("scanned " << filesScanned << " files, " << literalsScanned
                    << " string literals, " << peakLiterals << " of them mentioning peak");
    REQUIRE(filesScanned > 0);
    REQUIRE(literalsScanned > 0);
    REQUIRE(peakLiterals > 0);

    for (const std::string& offender : offenders) {
        INFO("unqualified SPL peak label: " << offender);
    }
    CHECK(offenders.empty());

    // Every exemption must still be real. A renamed literal leaves a stale
    // entry here, and a stale entry is a hole in the scan.
    for (const Allowed& a : allowed) {
        INFO("exemption \"" << a.literal << "\" (" << a.why << ") must still be found");
        CHECK(a.seen > 0);
    }

    // The two halves of the rule cannot drift, because there is one rule: every
    // token E2 accepts is a token the header's own predicate accepts, and every
    // criterion's shipped label satisfies both.
    for (std::string_view token : kQuantityTokens) {
        INFO("kQuantityTokens entry " << token);
        CHECK(labelNamesItsQuantity(token));
    }
    for (const SplCriterion& c : kPeakAndCeilingCriteria) {
        INFO(c.framework << " label " << c.readoutLabel);
        bool acceptedByE2 = false;
        const std::string lower = lowered(std::string(c.readoutLabel));
        for (std::string_view token : kQuantityTokens) {
            if (lower.find(lowered(std::string(token))) != std::string::npos) {
                acceptedByE2 = true;
                break;
            }
        }
        CHECK(acceptedByE2);
        CHECK(labelNamesItsQuantity(c.readoutLabel));
    }

    std::ostringstream report;
    report << "E2 scanned " << filesScanned << " files / " << literalsScanned
           << " string literals under app/src/export and app/src/view; "
           << peakLiterals << " mention peak, " << (sizeof(allowed) / sizeof(allowed[0]))
           << " exempt as EQ/FIR vocabulary, " << offenders.size()
           << " unqualified SPL peak labels; " << kQuantityTokens.size()
           << " quantity tokens, read from SplCriteria.h rather than copied";
    WARN(report.str());
}

// --- E3: sampled, and it says so -----------------------------------------

TEST_CASE("E3 the peak readout label carries the word sampled", "[splcriteria]") {
    CHECK(lowered(std::string(kSampledPeakLabel)).find("sampled") != std::string::npos);
    // And it still names its quantity, so it satisfies E2's rule as well.
    CHECK(kSampledPeakLabel.find("L_Cpeak") != std::string_view::npos);
    CHECK(labelNamesItsQuantity(kSampledPeakLabel));

    // The only peak this project can cite a standard for. IEC 61672-1
    // specifies the ACCURACY of a peak detector only for C weighting
    // (ISO 1996-1 cl. 3.1.4 Note 3), so L_Zpeak and L_Apeak are computable and
    // unspecified.
    CHECK(kReportablePeakLabel == "L_Cpeak");

    SECTION("a bare label is rejected by the guard the readouts use") {
        CHECK_FALSE(labelNamesItsQuantity("peak"));
        CHECK_FALSE(labelNamesItsQuantity("Peak"));
        CHECK_FALSE(labelNamesItsQuantity("Peak (dB)"));
        CHECK_FALSE(labelNamesItsQuantity(""));
        CHECK(labelNamesItsQuantity("L_Cpeak"));
    }
}

// --- the dose presets: the shipped q is COMPUTED (D1f, in app) -----------

TEST_CASE("W1-D the two shipped dose presets carry computed exchange denominators",
          "[splcriteria]") {
    // D1f proves core's `exchangeDenominator` computes. THIS proves the value
    // that actually ships in the config does too -- which is the half of
    // SPL-R7 that a core-only test cannot reach, because core holds no preset.
    SplConfig config;
    REQUIRE(config.dose.size() == 2);

    const rta::meter::DoseSettings& niosh = config.dose[0];
    CHECK_THAT(niosh.criterionLevelDb, WithinAbs(85.0, 1e-12));
    CHECK_THAT(niosh.criterionSeconds, WithinAbs(8.0 * 3600.0, 1e-12));
    CHECK_THAT(niosh.thresholdDb, WithinAbs(80.0, 1e-12));
    CHECK(niosh.q == rta::meter::exchangeDenominator(3.0));
    CHECK(niosh.q == 3.0 / std::log10(2.0));
    CHECK_FALSE(niosh.q == 9.9657843);

    const rta::meter::DoseSettings& osha = config.dose[1];
    CHECK_THAT(osha.criterionLevelDb, WithinAbs(90.0, 1e-12));
    CHECK_THAT(osha.criterionSeconds, WithinAbs(8.0 * 3600.0, 1e-12));
    CHECK_THAT(osha.thresholdDb, WithinAbs(90.0, 1e-12));
    CHECK(osha.q == rta::meter::exchangeDenominator(5.0));
    CHECK(osha.q == 5.0 / std::log10(2.0));
    CHECK_FALSE(osha.q == 16.6096404);

    SECTION("two accumulators, and they are NOT the same settings") {
        CHECK_FALSE(niosh.q == osha.q);
        CHECK_FALSE(niosh.criterionLevelDb == osha.criterionLevelDb);
        CHECK_FALSE(niosh.thresholdDb == osha.thresholdDb);
    }

    SECTION("the presets are DATA here and named nowhere in core") {
        // Record section 11's split: core holds the formula and four numbers,
        // app holds which four. test_dose.cpp D3b is the other half.
        std::ostringstream key;
        key << "niosh q=" << niosh.q << " osha q=" << osha.q;
        INFO(key.str());
        CHECK(niosh.q > 9.0);
        CHECK(osha.q > 16.0);
    }
}
