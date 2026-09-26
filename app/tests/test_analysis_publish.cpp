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

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "measure/AnalysisPublish.h"
#include "measure/Analyser.h"
#include "measure/AverageGroup.h"
#include "measure/RoutingPlan.h"

#include "rta/dsp/SpatialAverage.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
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

TEST_CASE("syncAverageGroupMembership caps membership at the analyser count",
          "[analysispublish]") {
    // A 64-channel interface (kMaxChannels) can route far more than
    // kMaxTransferFunctions measurement channels against ONE shared reference.
    // AnalysisThread builds exactly kMaxTransferFunctions Analysers and never
    // more; a route past that position has no Analyser and no audio
    // (drainPaired's own `routeIndex >= kMaxTransferFunctions` guard), so it
    // must not become a group member. Before this cap it did, and
    // publishAverageGroup then indexed analysers[kMaxTransferFunctions..] on a
    // vector exactly that size -- an out-of-bounds read reachable on real
    // multichannel hardware, not a theoretical one.
    AverageGroup group;
    std::vector<int> lastTfIndices;
    const RoutingPlan plan = makeSharedReferencePlan(rta::measure::kMaxTransferFunctions + 2);

    const auto indices = syncAverageGroupMembership(group, lastTfIndices, plan);
    REQUIRE(indices.size() == static_cast<std::size_t>(rta::measure::kMaxTransferFunctions));
    REQUIRE(group.memberCount() == static_cast<std::size_t>(rta::measure::kMaxTransferFunctions));
    for (const std::size_t idx : indices) {
        CHECK(idx < static_cast<std::size_t>(rta::measure::kMaxTransferFunctions));
    }
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

TEST_CASE("a route past the analyser cap gets an ExcludedOverCapacity summary, not a crash",
          "[analysispublish]") {
    // AnalysisThread builds exactly kMaxTransferFunctions Analysers. Mirror
    // that here -- exactly the cap, never more -- while routing TWO MORE than
    // the cap against one shared reference. Every route still gets a summary:
    // the first kMaxTransferFunctions are members, the surplus are
    // ExcludedOverCapacity, and buildPublishedSnapshot must never index an
    // Analyser it does not have (the out-of-bounds read the cap closes).
    const int cap = rta::measure::kMaxTransferFunctions;
    auto analysers = makeEngagedAnalysers(cap);  // exactly cap, as AnalysisThread does
    AverageGroup group;
    std::vector<int> lastTfIndices;
    const RoutingPlan plan = makeSharedReferencePlan(cap + 2);

    std::uint64_t droppedSamples = 0;
    const auto snapshot = buildPublishedSnapshot(analysers, group, lastTfIndices, plan, droppedSamples);

    REQUIRE(snapshot->positions.size() == static_cast<std::size_t>(cap + 2));
    for (int i = 0; i < cap; ++i) {
        CHECK(snapshot->positions[static_cast<std::size_t>(i)].membership == Membership::Member);
    }
    CHECK(snapshot->positions[static_cast<std::size_t>(cap)].membership ==
          Membership::ExcludedOverCapacity);
    CHECK(snapshot->positions[static_cast<std::size_t>(cap + 1)].membership ==
          Membership::ExcludedOverCapacity);
    // The average is built from the cap real members only, no NaN from the
    // surplus routes that were never gathered.
    REQUIRE(snapshot->average.has_value());
    for (const float v : snapshot->average->magnitudeDb) CHECK(std::isfinite(v));
}

TEST_CASE("the live average excludes a refused route by number, not just by label",
          "[analysispublish]") {
    // The F3 summary test above proves a refused route gets its own summary;
    // it CANNOT prove the average left the route's data out, because there all
    // three positions carried identical audio -- a 2-way and a 3-way average
    // are the same number, so a leak would be invisible
    // (memory/a-fixture-can-be-too-well-behaved-to-fail.md). This is the
    // discriminating fixture: positions 0 and 1 (reference 0, the live group)
    // are driven at 0.7 gain; position 2 names reference 5 (REFUSED) AND is
    // driven at 0.1 gain, so including it WOULD move the average measurably.
    Analyser::Config cfg = fastConfig();
    std::vector<std::unique_ptr<Analyser>> analysers;
    for (int i = 0; i < 3; ++i) analysers.push_back(std::make_unique<Analyser>(cfg));

    std::vector<float> reference(64), meas07(64), meas01(64);
    for (std::size_t i = 0; i < 64; ++i) {
        const double t = static_cast<double>(i);
        reference[i] = static_cast<float>(std::sin(2.0 * 3.14159265358979 * 6.0 * t / 64.0));
        meas07[i] = reference[i] * 0.7f;
        meas01[i] = reference[i] * 0.1f;  // a very different transfer magnitude
    }
    for (int frame = 0; frame < 30; ++frame) {
        analysers[0]->pushPair(reference, meas07);
        analysers[1]->pushPair(reference, meas07);
        analysers[2]->pushPair(reference, meas01);
    }

    AverageGroup group;
    std::vector<int> lastTfIndices;
    RoutingPlan plan;
    plan.routes.push_back(TransferRoute{0, 0, 1});  // reference 0 -- member
    plan.routes.push_back(TransferRoute{1, 0, 2});  // reference 0 -- member
    plan.routes.push_back(TransferRoute{2, 5, 3});  // reference 5 -- refused
    plan.distinctReferences = {0, 5};

    std::uint64_t droppedSamples = 0;
    const auto snapshot = buildPublishedSnapshot(analysers, group, lastTfIndices, plan, droppedSamples);
    REQUIRE(snapshot->average.has_value());

    // Independent expectation: the spatial average over ONLY the two members'
    // own snapshots, with their default unit trims. Same core function the
    // publish path uses -- so this is a regression lock proving the publish
    // path fed spatialAverage exactly the two members, never the refused
    // third, not an independent oracle for spatialAverage's arithmetic (that
    // is core/tests/golden/spatial.txt).
    const std::vector<double> twoWeights{1.0, 1.0};
    const std::vector<rta::dsp::TransferSnapshot> twoMembers{
        analysers[0]->transferSnapshotForAverage(), analysers[1]->transferSnapshotForAverage()};
    const auto expectedTwoWay = rta::dsp::spatialAverage(twoMembers, twoWeights);
    REQUIRE(expectedTwoWay.has_value());
    REQUIRE(snapshot->average->magnitudeDb.size() == expectedTwoWay->magnitudeDb.size());
    for (std::size_t k = 0; k < expectedTwoWay->magnitudeDb.size(); ++k) {
        CHECK(snapshot->average->magnitudeDb[k] ==
              Catch::Approx(expectedTwoWay->magnitudeDb[k]).margin(1e-4));
    }

    // And the fixture can fail: a 3-way average (the leak this test guards
    // against) is a DIFFERENT curve, so an implementation that let the refused
    // route into the average would break the equality above.
    const std::vector<double> threeWeights{1.0, 1.0, 1.0};
    const std::vector<rta::dsp::TransferSnapshot> threeMembers{
        analysers[0]->transferSnapshotForAverage(), analysers[1]->transferSnapshotForAverage(),
        analysers[2]->transferSnapshotForAverage()};
    const auto threeWay = rta::dsp::spatialAverage(threeMembers, threeWeights);
    REQUIRE(threeWay.has_value());
    bool differsSomewhere = false;
    for (std::size_t k = 0; k < threeWay->magnitudeDb.size(); ++k) {
        if (std::abs(threeWay->magnitudeDb[k] - expectedTwoWay->magnitudeDb[k]) > 1e-2f) {
            differsSomewhere = true;
            break;
        }
    }
    CHECK(differsSomewhere);
}

// --- LOW follow-up batch, item 3: fillSplPublishScalars ---------------------

TEST_CASE("fillSplPublishScalars copies every field it owns, independently",
          "[analysispublish]") {
    // Pulled out of AnalysisThreadSpl.cpp so this reaches RTA_BUILD_APP=OFF --
    // see AnalysisPublish.h's own comment on the function. Every argument is
    // a DISTINCT, non-default value, so dropping ANY ONE assignment leaves
    // that field at SplPublishInput's own default (0/false/nullptr), which
    // this case catches; a shared all-true/all-zero fixture could not.
    //
    // Fix round, LOW finding: the three bools used to share ONE value per
    // call (all true, then all false), so a swap between any two of them
    // went uncaught. Varied independently (true/false/true, then
    // false/true/false) so a swap flips exactly one CHECK.
    rta::measure::SplConfig config;
    const rta::measure::SplChannelState channelState(config, 48000.0);
    rta::measure::SplPublishInput input;
    rta::measure::fillSplPublishScalars(input, 3, 5, true, &channelState, 7, false, true);
    CHECK(input.refusedMetrics == 3);
    CHECK(input.overflowedLnTicks == 5);
    CHECK(input.blockSecondsTooSmall == true);
    CHECK(input.channelState == &channelState);
    CHECK(input.logDroppedBlocks == 7);
    CHECK(input.logWriteFailed == false);
    CHECK(input.calibrationInvalid == true);

    // The other bool pattern, with the numeric/pointer fields also cleared
    // to their opposite (zero/nullptr) side, so a mutation hardcoding any
    // one field to a fixed value is caught either way.
    rta::measure::SplPublishInput cleared;
    rta::measure::fillSplPublishScalars(cleared, 0, 0, false, nullptr, 0, true, false);
    CHECK(cleared.refusedMetrics == 0);
    CHECK(cleared.overflowedLnTicks == 0);
    CHECK_FALSE(cleared.blockSecondsTooSmall);
    CHECK(cleared.channelState == nullptr);
    CHECK(cleared.logDroppedBlocks == 0);
    CHECK(cleared.logWriteFailed == true);
    CHECK_FALSE(cleared.calibrationInvalid);
}
