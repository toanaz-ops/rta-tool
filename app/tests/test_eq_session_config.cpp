// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-EQ UI tasks T0 and T1 (docs/plans/2026-09-29-eq-ui-lane-plan.md): the two
// EqSession additions the EQ pane needs -- levelOffsetDb() (the auto-offset c
// the allocator aims with, exposed so the pane can draw "target + c") and
// setMaxFilters() (the BANDS control). JUCE-free, like the rest of the session.
#include "EqSessionFixture.h"

#include "measure/EqSession.h"

#include "rta/eq/BiquadDesign.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

using eqfixture::bumpDb;
using eqfixture::kFs;
using eqfixture::logGrid;
using rta::eq::FilterSpec;
using rta::eq::FilterType;
using rta::eq::responseDb;
using rta::measure::EqSession;

TEST_CASE("EqSession::levelOffsetDb is the gamma^2/f-weighted mean of measured - target",
          "[eq_session_config]") {
    // Two trusted bins at 100 and 200 Hz, gamma^2 = 1, residual 0 and 3 dB.
    // Weights 1/100 and 1/200, so c = (0.01*0 + 0.005*3)/0.015 = 1 (record
    // Sec.3 step 1, worked by hand). An unweighted mean would be 1.5.
    const std::vector<float> hz{ 100.0f, 200.0f };
    const std::vector<float> measured{ 0.0f, 3.0f };
    const std::vector<float> target{ 0.0f, 0.0f };
    const std::vector<float> coherence{ 1.0f, 1.0f };

    EqSession session;
    session.setMeasurement(hz, measured, target, coherence, {}, kFs);
    CHECK(session.levelOffsetDb() == Catch::Approx(1.0).margin(1e-12));

    SECTION("a committed, NOT-applied filter moves c by its weighted mean response") {
        // c is taken over the WORKING residual, which carries every
        // un-applied filter (EqSession's one membership rule) -- so c is
        // linear in the residual: c = c0 + sum_k w_k R(f_k) / sum_k w_k.
        session.acceptCandidate(rta::eq::Candidate{ FilterSpec{ FilterType::Peaking, 150.0, 1.0, 4.0 }, 0.0, {} });
        const double w1 = 1.0 / 100.0;
        const double w2 = 1.0 / 200.0;
        const double r1 = responseDb(session.committed().front().spec, kFs, 100.0);
        const double r2 = responseDb(session.committed().front().spec, kFs, 200.0);
        const double expected = 1.0 + (w1 * r1 + w2 * r2) / (w1 + w2);
        CHECK(session.levelOffsetDb() == Catch::Approx(expected).margin(1e-9));

        // ...and an APPLIED filter is already in the measurement, so it
        // leaves c exactly where it was.
        session.markApplied(0, true);
        CHECK(session.levelOffsetDb() == Catch::Approx(1.0).margin(1e-12));
    }
    SECTION("no coherence vector: nothing is trusted, so 0 -- not a throw") {
        session.setMeasurement(hz, measured, target, {}, {}, kFs);
        CHECK(session.levelOffsetDb() == 0.0);
    }
}

TEST_CASE("EqSession::levelOffsetDb before any measurement is 0", "[eq_session_config]") {
    EqSession session;
    CHECK(session.levelOffsetDb() == 0.0);
}

namespace {

// Three well-separated 8 dB bumps (250 Hz, 1 kHz, 4 kHz; 0.25 octave wide, so
// their half-gain flanks never meet).
eqfixture::Fixture threeBumps() {
    eqfixture::Fixture f;
    f.hz = logGrid(192, 20.0, 20000.0);
    f.measuredDb.assign(f.hz.size(), 0.0f);
    for (const double centre : { 250.0, 1000.0, 4000.0 }) {
        const auto bump = bumpDb(f.hz, centre, 8.0, 0.25);
        for (std::size_t k = 0; k < f.hz.size(); ++k) f.measuredDb[k] += bump[k];
    }
    f.targetDb.assign(f.hz.size(), 0.0f);
    f.coherence.assign(f.hz.size(), 0.95f);
    return f;
}

}  // namespace

TEST_CASE("EqSession::setMaxFilters caps what Auto EQ commits", "[eq_session_config]") {
    const auto f = threeBumps();

    EqSession capped;
    eqfixture::load(capped, f);
    capped.setMaxFilters(2);
    capped.runAutoEq();
    // The cap is the whole reason for the setter: three bumps are on offer
    // and exactly two land.
    CHECK(capped.committed().size() == 2);
    CHECK(capped.config().maxFilters == 2);

    SECTION("out-of-range inputs clamp to [1, 16]") {
        capped.setMaxFilters(0);
        CHECK(capped.config().maxFilters == 1);
        capped.setMaxFilters(17);
        CHECK(capped.config().maxFilters == 16);
        capped.setMaxFilters(-5);
        CHECK(capped.config().maxFilters == 1);
    }
    SECTION("raising the cap again lets the next Auto EQ place more") {
        capped.setMaxFilters(3);
        capped.runAutoEq();  // replaces its own un-applied suggestion
        CHECK(capped.committed().size() >= 3);
    }
}

TEST_CASE("EqSession::levelOffsetDb is 0 before any measurement, not a throw", "[eq_session_config]") {
    // The allocator's validate() refuses an empty grid; the pane draws
    // "target + c" while nothing is picked yet, so the session answers 0.
    const EqSession session;
    CHECK(session.levelOffsetDb() == 0.0);
}
