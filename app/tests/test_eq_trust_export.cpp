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
constexpr double kFs = 48000.0;
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
    REQUIRE(parsed.size() == specs.size());
    for (std::size_t i = 0; i < specs.size(); ++i) {
        CHECK(parsed[i].type == specs[i].type);
        // The written precision IS the round-trip precision: a whole hertz,
        // 0.01 of Q, 0.1 dB. Anything finer would be a readout the format
        // does not carry.
        CHECK(std::abs(parsed[i].fcHz - specs[i].fcHz) <= 0.5);
        CHECK(std::abs(parsed[i].q - specs[i].q) <= 0.005);
        CHECK(std::abs(parsed[i].gainDb - specs[i].gainDb) <= 0.05);
    }
}
