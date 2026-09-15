// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-EQ task E (docs/plans/2026-09-07-L7-eq-impl-plan.md; decision record
// docs/dsp/2026-09-06-l7-auto-eq.md sec.2, sec.7). The app-side session model
// and nothing else: Auto EQ one-shot, Suggest accept/decline/re-rank, and the
// one membership rule the ghost and the working residual share. The coherence
// trust mask and the text format live in test_eq_trust_export.cpp. JUCE-free
// like OutputPolicy/DelayLocator, so the whole file is provable with no device
// and no GUI.

#include "export/EqTextExport.h"
#include "measure/EqSession.h"

#include "rta/eq/BiquadDesign.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using rta::eq::FilterSpec;
using rta::eq::FilterType;
using rta::eq::responseDb;
using rta::measure::EqSession;
using rta::measure::EqSessionConfig;

namespace {

constexpr double kFs = 48000.0;

/// A log-spaced measurement grid. Ascending, strictly positive, below
/// Nyquist -- the only three things EqInput asks of `hz`.
std::vector<float> logGrid(std::size_t bins, double lowHz, double highHz) {
    std::vector<float> hz(bins);
    const double step = std::log10(highHz / lowHz) / static_cast<double>(bins - 1);
    for (std::size_t k = 0; k < bins; ++k) {
        hz[k] = static_cast<float>(lowHz * std::pow(10.0, step * static_cast<double>(k)));
    }
    return hz;
}

/// One Gaussian-in-log bump: a smooth, single-extremum deviation, so greedy
/// placement has exactly one obvious first move and the half-gain crossings
/// the Q rule walks out to exist on both flanks.
std::vector<float> bumpDb(std::span<const float> hz, double centreHz, double heightDb,
                          double widthOctaves) {
    std::vector<float> m(hz.size(), 0.0f);
    for (std::size_t k = 0; k < hz.size(); ++k) {
        const double octaves = std::log2(static_cast<double>(hz[k]) / centreHz);
        const double z = octaves / widthOctaves;
        m[k] = static_cast<float>(heightDb * std::exp(-0.5 * z * z));
    }
    return m;
}

struct Fixture {
    std::vector<float> hz;
    std::vector<float> measuredDb;
    std::vector<float> targetDb;
    std::vector<float> coherence;
};

Fixture makeFixture(float coherenceValue = 0.95f) {
    Fixture f;
    f.hz = logGrid(192, 20.0, 20000.0);
    f.measuredDb = bumpDb(f.hz, 1000.0, 8.0, 0.5);
    f.targetDb.assign(f.hz.size(), 0.0f);
    f.coherence.assign(f.hz.size(), coherenceValue);
    return f;
}

void load(EqSession& session, const Fixture& f) {
    session.setMeasurement(f.hz, f.measuredDb, f.targetDb, f.coherence, {}, kFs);
}

}  // namespace

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

// --- E3 ---------------------------------------------------------------------
TEST_CASE("EqSession: accepting re-bases the residual; an applied mark drops its term") {
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

// --- E5 ---------------------------------------------------------------------
TEST_CASE("EqTextExport: a session's committed set is what gets exported") {
    const Fixture f = makeFixture();
    EqSessionConfig config;
    config.maxFilters = 2;
    EqSession session(config);
    load(session, f);
    session.runAutoEq();

    std::vector<FilterSpec> specs;
    for (const auto& committed : session.committed()) specs.push_back(committed.spec);
    REQUIRE_FALSE(specs.empty());

    const auto parsed =
            rta::eqexport::parseFilterList(rta::eqexport::renderFilterList(specs, kFs));
    REQUIRE(parsed.size() == specs.size());
}
