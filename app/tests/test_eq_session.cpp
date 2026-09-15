// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-EQ task E (docs/plans/2026-09-07-L7-eq-impl-plan.md; decision record
// docs/dsp/2026-09-06-l7-auto-eq.md sec.2, sec.7). The app-side session
// SEMANTICS and nothing else: the one membership rule the ghost and the
// working residual share, and what it means to mark a filter applied. The
// session lifecycle (decline, re-measure, export) is in
// test_eq_session_lifecycle.cpp; the coherence mask and the text format are in
// test_eq_trust_export.cpp. JUCE-free like OutputPolicy/DelayLocator, so the
// whole file is provable with no device and no GUI.

#include "EqSessionFixture.h"

#include "measure/EqSession.h"

#include "rta/eq/BiquadDesign.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

using eqfixture::Fixture;
using eqfixture::kFs;
using eqfixture::load;
using eqfixture::makeFixture;
using rta::eq::FilterSpec;
using rta::eq::responseDb;
using rta::measure::EqSession;
using rta::measure::EqSessionConfig;

// --- E1 ---------------------------------------------------------------------
TEST_CASE("EqSession: the ghost is the exact dB sum of the NOT-YET-APPLIED filters") {
    const Fixture f = makeFixture();
    EqSessionConfig config;
    config.maxFilters = 3;
    EqSession session(config);
    load(session, f);

    session.runAutoEq();
    REQUIRE_FALSE(session.committed().empty());

    const std::vector<double> ghost = session.ghostDb();
    REQUIRE(ghost.size() == f.hz.size());

    for (std::size_t k = 0; k < f.hz.size(); ++k) {
        double expected = static_cast<double>(f.measuredDb[k]);
        for (const auto& committed : session.committed()) {
            if (committed.applied) continue;
            expected += responseDb(committed.spec, kFs, static_cast<double>(f.hz[k]));
        }
        // Exact dB add in double (record sec.7's ghost identity): the only
        // slack allowed is the order the same terms were summed in.
        CHECK(std::abs(ghost[k] - expected) <= 1e-9);
    }
}

TEST_CASE("EqSession: ghost minus target IS the working residual, applied or not") {
    // The one identity that pins the meaning of measuredDb_ (EqSession.h's
    // "one rule, both sums"). ghost and residual read the same measurement and
    // the same committed list, so they can differ ONLY by the target:
    //
    //     ghost_k - t_k == residual_k     for every k, at every applied state.
    //
    // A ghost that sums a filter the residual skips breaks this by exactly
    // that filter's response -- which is the shape of the defect this case
    // exists to catch, not a restatement of either function's own arithmetic.
    const Fixture f = makeFixture();
    EqSessionConfig config;
    config.maxFilters = 3;
    EqSession session(config);
    load(session, f);
    session.runAutoEq();
    REQUIRE(session.committed().size() >= 2);

    auto checkIdentity = [&](const char* whenLabel) {
        INFO(whenLabel);
        const std::vector<double> ghost = session.ghostDb();
        const std::vector<float> residual = session.workingResidualDb();
        REQUIRE(ghost.size() == residual.size());
        for (std::size_t k = 0; k < ghost.size(); ++k) {
            const double lhs = ghost[k] - static_cast<double>(f.targetDb[k]);
            CHECK(std::abs(lhs - static_cast<double>(residual[k])) <= 1e-5);
        }
    };

    checkIdentity("nothing applied yet");
    session.markApplied(0, true);
    checkIdentity("first filter marked applied");
    session.markApplied(1, true);
    checkIdentity("two filters marked applied");
}

TEST_CASE("EqSession: Auto EQ leaves the ghost closer to target than the measurement") {
    const Fixture f = makeFixture();
    EqSessionConfig config;
    config.maxFilters = 3;
    EqSession session(config);
    load(session, f);
    session.runAutoEq();

    const std::vector<double> ghost = session.ghostDb();
    double before = 0.0;
    double after = 0.0;
    for (std::size_t k = 0; k < f.hz.size(); ++k) {
        const double m = static_cast<double>(f.measuredDb[k]);
        before += m * m;
        after += ghost[k] * ghost[k];
    }
    // The bump is 8 dB of pure magnitude error against a flat target: peaking
    // filters cannot fail to shrink it unless the sign convention is inverted
    // (HANDOFF's load-bearing point 1 -- EqAllocator negates before solving).
    CHECK(after < before);
}

// --- E3 ---------------------------------------------------------------------
TEST_CASE("EqSession: accepting re-bases the residual; an applied mark drops its term "
          "(TRANSIENT: before the re-measurement lands)") {
    const Fixture f = makeFixture();
    EqSession session;
    load(session, f);

    const auto first = session.suggest(1);
    REQUIRE_FALSE(first.empty());
    session.acceptCandidate(first.front());
    REQUIRE(session.committed().size() == 1);
    const FilterSpec accepted = session.committed().front().spec;

    {
        const auto residual = session.workingResidualDb();
        REQUIRE(residual.size() == f.hz.size());
        for (std::size_t k = 0; k < f.hz.size(); ++k) {
            const double expected = static_cast<double>(f.measuredDb[k])
                                    - static_cast<double>(f.targetDb[k])
                                    + responseDb(accepted, kFs, static_cast<double>(f.hz[k]));
            CHECK(std::abs(static_cast<double>(residual[k]) - expected) <= 1e-5);
        }
    }

    // Marked applied: the correction now lives in the room, so counting it in
    // the prediction as well would count it twice (record sec.2). BOTH sums
    // drop the term -- the residual AND the ghost -- because both read the
    // same measurement, and that measurement is the one the operator will
    // re-take with the filter already in the signal path.
    //
    // THIS IS THE TRANSIENT STATE, not the resting one. The mark has been set
    // and the re-measurement has NOT arrived, so both sums fall back to the
    // stale, uncorrected measurement and the ghost jumps back up by the full
    // filter gain. That is arithmetically right and operationally loud -- at a
    // rig this state can last minutes, the length of step 4 of the operator's
    // five-step loop. The resting state is the next test case, where the
    // re-measurement lands. Do not read what follows as "the ghost after
    // applying a filter"; read it as "the ghost while the tool is waiting to
    // be told what the room now measures".
    session.markApplied(0, true);
    {
        const auto residual = session.workingResidualDb();
        const auto ghost = session.ghostDb();
        for (std::size_t k = 0; k < f.hz.size(); ++k) {
            const double expectedResidual = static_cast<double>(f.measuredDb[k])
                                            - static_cast<double>(f.targetDb[k]);
            CHECK(std::abs(static_cast<double>(residual[k]) - expectedResidual) <= 1e-5);
            // No un-applied filter is left, so the ghost is the measurement
            // itself -- it predicts no further change, which is exactly what
            // "everything I suggested is already in the rig" means.
            CHECK(std::abs(ghost[k] - static_cast<double>(f.measuredDb[k])) <= 1e-9);
        }
    }
}

TEST_CASE("EqSession: an applied filter is never re-suggested at the same fc and gain") {
    // The operator's real loop: measure, accept, dial the filter into the rig,
    // mark it applied, RE-MEASURE. The second measurement already carries the
    // correction, so the session must not propose it a second time -- landing
    // it twice would put double the cut on the same bump.
    const Fixture pre = makeFixture();
    EqSession session;
    load(session, pre);

    const auto first = session.suggest(1);
    REQUIRE_FALSE(first.empty());
    session.acceptCandidate(first.front());
    REQUIRE(session.committed().size() == 1);
    const FilterSpec applied = session.committed().front().spec;
    session.markApplied(0, true);

    // The room now measures m + R_applied: that is what "applied" asserts.
    Fixture post = pre;
    for (std::size_t k = 0; k < post.hz.size(); ++k) {
        post.measuredDb[k] = static_cast<float>(
                static_cast<double>(pre.measuredDb[k])
                + responseDb(applied, kFs, static_cast<double>(post.hz[k])));
    }
    load(session, post);

    // A new measurement does NOT wipe the session: the committed list and its
    // applied marks are what make the new measurement interpretable at all.
    REQUIRE(session.committed().size() == 1);
    CHECK(session.committed().front().applied);

    // Closed form, both sums, at the new baseline: nothing un-applied is left,
    // so the residual is the raw deviation of the NEW measurement and the
    // ghost is that measurement itself.
    {
        const auto residual = session.workingResidualDb();
        const auto ghost = session.ghostDb();
        for (std::size_t k = 0; k < post.hz.size(); ++k) {
            CHECK(std::abs(static_cast<double>(residual[k])
                           - (static_cast<double>(post.measuredDb[k])
                              - static_cast<double>(post.targetDb[k])))
                  <= 1e-5);
            CHECK(std::abs(ghost[k] - static_cast<double>(post.measuredDb[k])) <= 1e-9);
        }
    }

    // The defect's signature was the SECOND ranking reproducing the first
    // exactly, because the residual handed to the allocator was bit-identical
    // to the one that produced it. The closed-form residual check above is
    // the unconditional half of the guard; this is the operational half.
    const auto next = session.suggest(3);
    INFO("suggestions after the re-measure: " << next.size());
    for (const auto& candidate : next) {
        const bool sameFilter = std::abs(candidate.spec.fcHz - applied.fcHz) < 1.0
                                && std::abs(candidate.spec.gainDb - applied.gainDb) < 0.1;
        CHECK_FALSE(sameFilter);
    }
}

