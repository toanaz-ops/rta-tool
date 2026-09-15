// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-EQ task E (docs/plans/2026-09-07-L7-eq-impl-plan.md; decision record
// docs/dsp/2026-09-06-l7-auto-eq.md sec.2, sec.4.1, sec.7). The app-side
// session model: Auto EQ one-shot, Suggest accept/decline/re-rank, the exact
// dB-add ghost, the plain coherence trust mask (EQ-R2) and the FilterSpec
// text export. JUCE-free like OutputPolicy/DelayLocator, so the whole file is
// provable with no device and no GUI.

#include "export/EqTextExport.h"
#include "measure/EqSession.h"
#include "measure/EqTrustMask.h"

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
using rta::measure::buildTrustMask;
using rta::measure::EqSession;
using rta::measure::EqSessionConfig;
using rta::measure::kEqTrustFloor;

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
TEST_CASE("EqSession: the ghost is the exact dB sum of every committed filter") {
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
            expected += responseDb(committed.spec, kFs, static_cast<double>(f.hz[k]));
        }
        // Exact dB add in double (record sec.7's ghost identity): the only
        // slack allowed is the order the same terms were summed in.
        CHECK(std::abs(ghost[k] - expected) <= 1e-9);
    }
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
    // the prediction as well would count it twice (record sec.2).
    session.markApplied(0, true);
    {
        const auto residual = session.workingResidualDb();
        for (std::size_t k = 0; k < f.hz.size(); ++k) {
            const double expected = static_cast<double>(f.measuredDb[k])
                                    - static_cast<double>(f.targetDb[k]);
            CHECK(std::abs(static_cast<double>(residual[k]) - expected) <= 1e-5);
        }
    }
}

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
