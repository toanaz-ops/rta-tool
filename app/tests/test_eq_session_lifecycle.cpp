// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-EQ task E, the session LIFECYCLE half (docs/plans/2026-09-07-L7-eq-impl-
// plan.md; decision record docs/dsp/2026-09-06-l7-auto-eq.md sec.2, sec.7).
// What declining refuses, what a re-measurement keeps, and what leaves the
// session as a file. Split from test_eq_session.cpp on 2026-09-15 when the
// round-2 verify cases pushed that file past the 400-line cap; the fixtures
// both halves share live in EqSessionFixture.h.
//
// The applied/ghost/residual SEMANTICS are the other half, next door.

#include "EqSessionFixture.h"

#include "export/EqTextExport.h"
#include "measure/EqSession.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

using eqfixture::Fixture;
using eqfixture::kFs;
using eqfixture::load;
using eqfixture::makeFixture;
using eqfixture::makeLinearFixture;
using rta::measure::EqSession;
using rta::measure::EqSessionConfig;

// --- E2 ---------------------------------------------------------------------
TEST_CASE("EqSession: declining a chip excludes its region and the next rank omits it") {
    const Fixture f = makeFixture();
    EqSession session;
    load(session, f);

    const auto first = session.suggest(1);
    REQUIRE_FALSE(first.empty());
    const double declinedFc = first.front().spec.fcHz;

    session.declineCandidate(first.front());

    const auto excluded = session.excluded();
    std::size_t excludedBins = 0;
    for (std::uint8_t e : excluded) excludedBins += (e != 0) ? 1u : 0u;
    CHECK(excludedBins > 0);

    // Every bin inside the declined filter's own -3 dB band is excluded.
    const auto band = rta::measure::filterBandHz(first.front().spec);
    for (std::size_t k = 0; k < f.hz.size(); ++k) {
        if (static_cast<double>(f.hz[k]) >= band.lowHz
            && static_cast<double>(f.hz[k]) <= band.highHz) {
            CHECK(excluded[k] != 0);
        }
    }

    const auto second = session.suggest(1);
    if (!second.empty()) {
        CHECK(second.front().spec.fcHz != declinedFc);
    }
}

TEST_CASE("EqSession: an untrusted band never carries a placement") {
    Fixture f = makeFixture();
    // Kill coherence over the bump itself: nothing is trusted where the error
    // is, so there is nothing legal left to place on there.
    for (std::size_t k = 0; k < f.hz.size(); ++k) {
        if (f.hz[k] > 500.0f && f.hz[k] < 2000.0f) f.coherence[k] = 0.4f;
    }
    EqSession session;
    load(session, f);

    for (const auto& candidate : session.suggest(3)) {
        CHECK((candidate.spec.fcHz <= 500.0 || candidate.spec.fcHz >= 2000.0));
    }
}

TEST_CASE("EqSession: a declined region survives a measurement on a different grid") {
    // setMeasurement keeps the declined regions, and keeps them as frequency
    // BANDS rather than bin flags precisely so a re-measurement at another FFT
    // size still refuses the same stretch of spectrum. Round 2 of the verify
    // found that claim had no coverage: deleting the behaviour left the whole
    // suite green. This is the lock.
    //
    // Both grids are real fixed-engine half-grids, DC..Nyquist, 2^k + 1 bins,
    // so bin k sits at exactly k * binWidth and the expected mask is a closed
    // form rather than a count read off the implementation.
    constexpr std::size_t kBinsA = 193;  // binWidth = 24000 / 192 = 125 Hz
    constexpr std::size_t kBinsB = 257;  // binWidth = 24000 / 256 = 93.75 Hz

    const Fixture a = makeLinearFixture(kBinsA);
    EqSession session;
    load(session, a);

    const auto first = session.suggest(1);
    REQUIRE_FALSE(first.empty());
    const auto band = rta::measure::filterBandHz(first.front().spec);
    session.declineCandidate(first.front());

    const Fixture b = makeLinearFixture(kBinsB);
    load(session, b);

    const auto excluded = session.excluded();
    REQUIRE(excluded.size() == kBinsB);

    // Closed form on the NEW grid: every bin whose centre frequency falls in
    // [lowHz, highHz] is excluded, and no other bin is.
    const double binWidthB = (kFs / 2.0) / static_cast<double>(kBinsB - 1);
    std::size_t excludedCount = 0;
    std::size_t expectedCount = 0;
    for (std::size_t k = 0; k < kBinsB; ++k) {
        const double f = static_cast<double>(k) * binWidthB;
        const bool inBand = (f >= band.lowHz && f <= band.highHz);
        CHECK((excluded[k] != 0) == inBand);
        excludedCount += (excluded[k] != 0) ? 1u : 0u;
        expectedCount += inBand ? 1u : 0u;
    }
    CHECK(excludedCount == expectedCount);
    CHECK(expectedCount > 0);  // the band must actually land on the new grid

    // clearExclusions is the only way back, and it is a different intent from
    // clearFilters -- both are exercised here so neither ships untested.
    session.clearExclusions();
    for (std::uint8_t e : session.excluded()) CHECK(e == 0);

    session.acceptCandidate(first.front());
    REQUIRE_FALSE(session.committed().empty());
    session.clearFilters();
    CHECK(session.committed().empty());
}

// --- E5 ---------------------------------------------------------------------
TEST_CASE("EqTextExport: a session's committed set is what gets exported") {
    const Fixture f = makeFixture();
    EqSessionConfig config;
    config.maxFilters = 2;
    EqSession session(config);
    load(session, f);
    session.runAutoEq();

    // What a UI does: hand the committed list to the exporter, applied bits
    // and all. The session's own type and the export's are the same shape on
    // purpose -- neither layer includes the other's header.
    session.markApplied(0, true);
    std::vector<rta::eqexport::ExportedFilter> filters;
    for (const auto& committed : session.committed()) {
        filters.push_back({ committed.spec, committed.applied });
    }
    REQUIRE(filters.size() >= 2);

    const auto parsed =
            rta::eqexport::parseFilterList(rta::eqexport::renderFilterList(filters, kFs));
    REQUIRE(parsed.size() == filters.size());
    // The bit that says "this one is already in the rig" reaches the file.
    CHECK(parsed[0].applied);
    CHECK_FALSE(parsed[1].applied);
}
