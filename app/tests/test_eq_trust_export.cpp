// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-EQ task E, the two pieces that are NOT the session state machine
// (docs/plans/2026-09-07-L7-eq-impl-plan.md; decision record
// docs/dsp/2026-09-06-l7-auto-eq.md sec.4.1 for the mask, sec.7 for the
// export). Split out of test_eq_session.cpp on 2026-09-15, when the verify
// round's extra cases pushed that file past the project's own "a file doing
// more than one job" line: the session's applied/ghost/residual semantics are
// one subject, a coherence floor and a text format are two others. JUCE-free
// like its sibling, so it builds in both configurations.

#include "export/EqTextExport.h"
#include "measure/EqTrustMask.h"

#include "rta/eq/FilterSpec.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using rta::eq::FilterSpec;
using rta::eq::FilterType;
using rta::measure::buildTrustMask;
using rta::measure::kEqTrustFloor;

namespace {
constexpr double kFs = 48000.0;  // local: this file builds no session fixture
}  // namespace

// --- E4 ---------------------------------------------------------------------
TEST_CASE("EqTrustMask: a plain coherence floor, and absent coherence is untrusted") {
    STATIC_REQUIRE(kEqTrustFloor == 0.7f);

    const std::vector<float> coherence{ 0.0f, 0.5f, 0.69f, 0.7f, 0.95f, 1.0f };
    const auto mask = buildTrustMask(coherence, coherence.size());
    REQUIRE(mask.size() == coherence.size());
    CHECK(mask[0] == 0);
    CHECK(mask[1] == 0);
    CHECK(mask[2] == 0);
    CHECK(mask[3] != 0);  // the floor itself is trusted: gamma^2 >= theta
    CHECK(mask[4] != 0);
    CHECK(mask[5] != 0);

    // Below the coherence gate a TransferSnapshot carries no coherence vector
    // at all (TransferEstimator.h): no vector is no evidence, which is a
    // refusal, not a pass.
    const auto absent = buildTrustMask({}, 6);
    REQUIRE(absent.size() == 6);
    for (std::uint8_t t : absent) CHECK(t == 0);

    // A plain floor and nothing else -- no ISO-2969 tolerance table is
    // involved (EQ-R2). Moving the floor moves exactly one boundary.
    const auto strict = buildTrustMask(coherence, coherence.size(), 0.96f);
    CHECK(strict[4] == 0);
    CHECK(strict[5] != 0);
}

// --- E5 ---------------------------------------------------------------------
TEST_CASE("EqTextExport: the filter list round-trips through its own text form") {
    const std::vector<FilterSpec> specs{
        { FilterType::Peaking, 1000.0, 1.41, -3.5 },
        { FilterType::LowShelf, 80.0, 0.71, 2.0 },
        { FilterType::HighShelf, 12500.0, 0.5, -1.2 },
    };

    const std::string text = rta::eqexport::renderFilterList(specs, kFs);

    // Project readout convention (CLAUDE.md "Reading out numbers"): frequency
    // is a whole number of hertz, dB keeps one decimal, Q two.
    CHECK(text.find(" 1000 ") != std::string::npos);
    CHECK(text.find("1000.0") == std::string::npos);
    CHECK(text.find("1.41") != std::string::npos);
    CHECK(text.find("-3.5") != std::string::npos);

    const auto parsed = rta::eqexport::parseFilterList(text);
    REQUIRE(parsed.filters.size() == specs.size());
    CHECK(parsed.rejectedLines.empty());
    for (std::size_t i = 0; i < specs.size(); ++i) {
        CHECK(parsed.filters[i].spec.type == specs[i].type);
        // The written precision IS the round-trip precision: a whole hertz,
        // 0.01 of Q, 0.1 dB. Anything finer would be a readout the format
        // does not carry.
        CHECK(std::abs(parsed.filters[i].spec.fcHz - specs[i].fcHz) <= 0.5);
        CHECK(std::abs(parsed.filters[i].spec.q - specs[i].q) <= 0.005);
        CHECK(std::abs(parsed.filters[i].spec.gainDb - specs[i].gainDb) <= 0.05);
        // The plain-FilterSpec overload means "nothing is in the rig yet".
        CHECK_FALSE(parsed.filters[i].applied);
    }
}

TEST_CASE("EqTextExport: an applied filter exports and re-imports as applied") {
    // The `applied` bit is a load-bearing session fact: it says the filter is
    // ALREADY in the signal path the measurement came through. A list that
    // drops it is a list that, loaded back into the DSP that produced the
    // measurement, applies that filter a SECOND time -- the arithmetic of the
    // round-1 ghost/residual defect, leaking out through the export boundary.
    const std::vector<rta::eqexport::ExportedFilter> filters{
        { { FilterType::Peaking, 994.0, 1.15, -7.1 }, true },
        { { FilterType::Peaking, 1480.0, 5.05, -1.1 }, false },
        { { FilterType::LowShelf, 80.0, 0.71, 2.0 }, true },
    };

    const std::string text = rta::eqexport::renderFilterList(filters, kFs);
    const auto parsed = rta::eqexport::parseFilterList(text);

    REQUIRE(parsed.filters.size() == filters.size());
    CHECK(parsed.rejectedLines.empty());
    for (std::size_t i = 0; i < filters.size(); ++i) {
        CHECK(parsed.filters[i].applied == filters[i].applied);
        CHECK(parsed.filters[i].spec.type == filters[i].spec.type);
        CHECK(std::abs(parsed.filters[i].spec.fcHz - filters[i].spec.fcHz) <= 0.5);
        CHECK(std::abs(parsed.filters[i].spec.gainDb - filters[i].spec.gainDb) <= 0.05);
    }

    // Visible to a human reading the file, not just to the parser -- asserted
    // on the ROW, because the header always contains the word "applied" and
    // an assertion over the whole text can never fail.
    CHECK(text.find("\npeaking 994 1.15 -7.1 applied\n") != std::string::npos);
    CHECK(text.find("\npeaking 1480 5.05 -1.1\n") != std::string::npos);

    // A row with no flag column reads as not-applied, so a list written by
    // hand, or by an older build, still imports.
    const auto legacy = rta::eqexport::parseFilterList(
            "# rta-eq filter list v1\npeaking 994 1.15 -7.1\n");
    REQUIRE(legacy.filters.size() == 1);
    CHECK_FALSE(legacy.filters.front().applied);
    CHECK(std::abs(legacy.filters.front().spec.fcHz - 994.0) <= 0.5);
}

TEST_CASE("EqTextExport: a Windows BOM does not rewrite the first row's filter type") {
    // Notepad and PowerShell 5.1's Out-File / Set-Content write a UTF-8 BOM by
    // default. It glues itself to the first token, so without a strip the
    // first row's type word is unrecognised -- and an unrecognised word must
    // never quietly become a Peaking: an 80 Hz lowshelf +2 dB importing as an
    // 80 Hz PEAKING +2 dB is a different filter, applied to the rig, that the
    // operator was never told about.
    const std::string bom = "\xEF\xBB\xBF";

    const auto withHeader = rta::eqexport::parseFilterList(
            bom + "# rta-eq filter list v1\nlowshelf 80 0.71 2.0 applied\n");
    REQUIRE(withHeader.filters.size() == 1);
    CHECK(withHeader.rejectedLines.empty());
    CHECK(withHeader.filters.front().spec.type == FilterType::LowShelf);
    CHECK(withHeader.filters.front().applied);

    // The nastier shape: the BOM lands directly on the data row.
    const auto bareRow = rta::eqexport::parseFilterList(bom + "lowshelf 80 0.71 2.0\n");
    REQUIRE(bareRow.filters.size() == 1);
    CHECK(bareRow.filters.front().spec.type == FilterType::LowShelf);

    // CRLF as well, since the same editors write it.
    const auto crlf = rta::eqexport::parseFilterList(
            bom + "# rta-eq filter list v1\r\nhighshelf 12500 0.50 -1.2 applied\r\n");
    REQUIRE(crlf.filters.size() == 1);
    CHECK(crlf.filters.front().spec.type == FilterType::HighShelf);
    CHECK(crlf.filters.front().applied);
}

TEST_CASE("EqTextExport: an unknown type word is rejected, never silently a Peaking") {
    const auto parsed = rta::eqexport::parseFilterList(
            "# rta-eq filter list v1\n"
            "bandpass 1000 1.00 -3.0\n"
            "peaking 1000 1.00 -3.0\n"
            "lowshel 80 0.71 2.0\n");  // one letter short: a real typo

    // Only the row this format can actually represent survives.
    REQUIRE(parsed.filters.size() == 1);
    CHECK(parsed.filters.front().spec.type == FilterType::Peaking);
    CHECK(std::abs(parsed.filters.front().spec.fcHz - 1000.0) <= 0.5);

    // And the rows that did not survive are NAMED, by 1-based line number --
    // a rejected row the caller cannot see is a row the operator silently
    // loses (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
    REQUIRE(parsed.rejectedLines.size() == 2);
    CHECK(parsed.rejectedLines[0] == 2);
    CHECK(parsed.rejectedLines[1] == 4);
}
