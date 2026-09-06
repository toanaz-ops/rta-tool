// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Task F2 (record §6): AnalysisThread::publishIfDue() calls exactly these
// two functions (measure/AnalysisPublish.h) to wire N routed positions into
// one AverageGroup publish. This file exercises the membership-sync half --
// which routes become group members, and how membership survives a re-sync
// -- with real RoutingPlans and real Analysers. The T12 byte-churn property
// (publishAverageGroup's own allocation is O(1) in N) is measured in
// test_average_group.cpp instead, which already owns the counting-allocator
// operator new/delete override for this whole test BINARY: a second such
// override here would be a duplicate-symbol link error, not a stronger test.

#include <catch2/catch_test_macros.hpp>

#include "measure/AnalysisPublish.h"
#include "measure/Analyser.h"
#include "measure/AverageGroup.h"
#include "measure/RoutingPlan.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

using rta::measure::Analyser;
using rta::measure::AverageGroup;
using rta::measure::buildPublishedSnapshot;
using rta::measure::Membership;
using rta::measure::publishAverageGroup;
using rta::measure::RoutingPlan;
using rta::measure::syncAverageGroupMembership;
using rta::measure::TransferRoute;

namespace {

/// A fast, real dual-FFT Analyser config -- same shape
/// test_average_group.cpp's own fastConfig() uses.
Analyser::Config fastConfig() {
    Analyser::Config config;
    config.fftSize = 64;
    config.hopSize = 64;
    config.sampleRate = 48000.0;
    config.mtwEnabled = false;
    config.transferFifoDepth = 8;
    return config;
}

/// `n` real Analysers, driven with enough paired frames to clear the
/// coherence gate -- test_average_group.cpp's first TEST_CASE fixture,
/// generalised to N.
std::vector<std::unique_ptr<Analyser>> makeEngagedAnalysers(int n) {
    std::vector<float> reference(64), measurement(64);
    for (std::size_t i = 0; i < 64; ++i) {
        const double t = static_cast<double>(i);
        reference[i] = static_cast<float>(std::sin(2.0 * 3.14159265358979 * 6.0 * t / 64.0));
        measurement[i] = reference[i] * 0.7f;
    }
    std::vector<std::unique_ptr<Analyser>> analysers;
    analysers.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        analysers.push_back(std::make_unique<Analyser>(fastConfig()));
    }
    for (int frame = 0; frame < 30; ++frame) {
        for (auto& a : analysers) a->pushPair(reference, measurement);
    }
    return analysers;
}

/// `n` routes, ascending measurement channel, all naming reference channel
/// 0 -- the common case record §6 describes ("every position names the same
/// reference and one read serves all").
RoutingPlan makeSharedReferencePlan(int n) {
    RoutingPlan plan;
    for (int i = 0; i < n; ++i) {
        plan.routes.push_back(TransferRoute{i, 0, i + 1});
    }
    plan.distinctReferences = {0};
    return plan;
}

}  // namespace

TEST_CASE("syncAverageGroupMembership builds one member per route sharing the reference",
          "[analysispublish]") {
    AverageGroup group;
    std::vector<int> lastTfIndices;
    const RoutingPlan plan = makeSharedReferencePlan(3);

    const auto indices = syncAverageGroupMembership(group, lastTfIndices, plan);
    REQUIRE(indices.size() == 3);
    REQUIRE(group.memberCount() == 3);
    CHECK(indices[0] == 0);
    CHECK(indices[1] == 1);
    CHECK(indices[2] == 2);
    CHECK(group.members()[0].tfIndex == 0);
    CHECK(group.members()[2].tfIndex == 2);
}

TEST_CASE("re-syncing the SAME plan does not rebuild membership", "[analysispublish]") {
    AverageGroup group;
    std::vector<int> lastTfIndices;
    const RoutingPlan plan = makeSharedReferencePlan(2);

    const auto first = syncAverageGroupMembership(group, lastTfIndices, plan);
    group.setSolo(1);  // an operator choice that a needless rebuild would wipe
    const auto second = syncAverageGroupMembership(group, lastTfIndices, plan);

    CHECK(first == second);
    CHECK(group.solo() == 1);  // survives because nothing was rebuilt
}

TEST_CASE("a route naming a different reference is excluded from the one live group",
          "[analysispublish]") {
    AverageGroup group;
    std::vector<int> lastTfIndices;
    RoutingPlan plan;
    plan.routes.push_back(TransferRoute{0, 0, 1});
    plan.routes.push_back(TransferRoute{1, 5, 2});  // a second reference
    plan.distinctReferences = {0, 5};

    const auto indices = syncAverageGroupMembership(group, lastTfIndices, plan);
    REQUIRE(indices.size() == 1);
    CHECK(indices[0] == 0);
    REQUIRE(group.memberCount() == 1);
    CHECK(group.members()[0].tfIndex == 0);
}

TEST_CASE("an empty plan clears any previous membership", "[analysispublish]") {
    AverageGroup group;
    std::vector<int> lastTfIndices;
    (void)syncAverageGroupMembership(group, lastTfIndices, makeSharedReferencePlan(2));
    REQUIRE(group.memberCount() == 2);

    const auto indices = syncAverageGroupMembership(group, lastTfIndices, RoutingPlan{});
    CHECK(indices.empty());
    CHECK(group.memberCount() == 0);
}

TEST_CASE("publishAverageGroup runs the real path: N real Analysers, one group publish",
          "[analysispublish]") {
    auto analysers = makeEngagedAnalysers(2);
    AverageGroup group;
    std::vector<int> lastTfIndices;
    const auto indices = syncAverageGroupMembership(group, lastTfIndices, makeSharedReferencePlan(2));

    const auto published = publishAverageGroup(group, indices, analysers);
    REQUIRE(published.average.has_value());
    REQUIRE(published.positions.size() == 2);
    CHECK(published.positions[0].gatePassed);
    CHECK(published.positions[1].gatePassed);
}

TEST_CASE("a not-yet-engaged member is excluded like an ungated one, not thrown", "[analysispublish]") {
    // Two Analysers, only the FIRST driven with pairs -- the second reads
    // transferSnapshotForAverage() with coherence == nullopt, exactly the
    // "excluded everywhere" state record §3 already defines, never a thrown
    // grid mismatch (this function's own header comment).
    std::vector<std::unique_ptr<Analyser>> analysers;
    analysers.push_back(std::make_unique<Analyser>(fastConfig()));
    analysers.push_back(std::make_unique<Analyser>(fastConfig()));

    std::vector<float> reference(64), measurement(64);
    for (std::size_t i = 0; i < 64; ++i) {
        const double t = static_cast<double>(i);
        reference[i] = static_cast<float>(std::sin(2.0 * 3.14159265358979 * 6.0 * t / 64.0));
        measurement[i] = reference[i] * 0.7f;
    }
    for (int frame = 0; frame < 30; ++frame) {
        analysers[0]->pushPair(reference, measurement);
    }

    AverageGroup group;
    std::vector<int> lastTfIndices;
    const auto indices = syncAverageGroupMembership(group, lastTfIndices, makeSharedReferencePlan(2));

    const auto published = publishAverageGroup(group, indices, analysers);
    REQUIRE(published.positions.size() == 2);
    CHECK(published.positions[0].gatePassed);
    CHECK_FALSE(published.positions[1].gatePassed);
    // The average must still exist -- position 0 alone contributes -- and
    // carry no NaN anywhere, the same guarantee spatialAverage's own tests
    // check at the core level.
    REQUIRE(published.average.has_value());
    for (const float v : published.average->magnitudeDb) CHECK(std::isfinite(v));
}

TEST_CASE("a route naming a different reference gets its own excluded summary, not silence",
          "[analysispublish]") {
    // Station-4 fix F3 (record §6, NOTE 1): before this fix,
    // syncAverageGroupMembership() filtered plan.routes down to the ones
    // sharing plan.routes.front()'s reference BEFORE ever calling
    // AverageGroup::addMember(), so a route naming a second reference was
    // dropped from buildPublishedSnapshot()'s output entirely -- no
    // PositionSummary, no average contribution, no field in Snapshot. This
    // is the RED this test was written against: two summaries, not three
    // (see this task's own commit message / docs/HANDOFF.md for the pasted
    // failure). The fix makes every route produce a summary, the excluded
    // one carrying Membership::ExcludedDifferentReference instead of
    // vanishing.
    auto analysers = makeEngagedAnalysers(3);
    AverageGroup group;
    std::vector<int> lastTfIndices;

    RoutingPlan plan;
    plan.routes.push_back(TransferRoute{0, 0, 1});  // reference 0 -- the live group
    plan.routes.push_back(TransferRoute{1, 0, 2});  // reference 0 -- the live group
    plan.routes.push_back(TransferRoute{2, 5, 3});  // reference 5 -- refused
    plan.distinctReferences = {0, 5};

    std::uint64_t droppedSamples = 0;
    const auto snapshot = buildPublishedSnapshot(analysers, group, lastTfIndices, plan, droppedSamples);

    REQUIRE(snapshot->positions.size() == 3);
    CHECK(snapshot->positions[0].membership == Membership::Member);
    CHECK(snapshot->positions[1].membership == Membership::Member);
    CHECK(snapshot->positions[2].membership == Membership::ExcludedDifferentReference);
    CHECK(snapshot->positions[2].tfIndex == 2);

    // The average is built from the two real members only -- the excluded
    // route contributes nothing, but its absence from the average is not
    // the same fact as its absence from the Snapshot (the bug conflated
    // the two).
    REQUIRE(snapshot->average.has_value());
    CHECK(snapshot->positions[0].gatePassed);
    CHECK(snapshot->positions[1].gatePassed);
}
