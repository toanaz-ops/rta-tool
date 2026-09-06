// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
#include "measure/AnalysisPublish.h"

#include <string>
#include <utility>

namespace rta::measure {

std::vector<std::size_t> syncAverageGroupMembership(AverageGroup& group,
                                                     std::vector<int>& lastTfIndices,
                                                     const RoutingPlan& plan) {
    if (plan.routes.empty()) {
        if (!lastTfIndices.empty()) {
            group = AverageGroup{};
            lastTfIndices.clear();
        }
        return {};
    }

    // Cheap, non-mutating PREDICTION of which routes would join the group,
    // using the group's own already-established reference when it has one
    // (an empty group would accept anything, so predict against the first
    // route's reference the same way `addMember` would for a brand new
    // group). This restates `AverageGroup::addMember`'s one-line rule
    // purely to answer "did anything routing-related change since last
    // sync", never to decide membership itself -- that decision is made
    // for real, by a real `addMember` call, in the rebuild below (station-4
    // fix F3: the old code filtered routes to this same prediction BEFORE
    // ever calling `addMember`, which made its `DifferentReference` refusal
    // unreachable in production).
    const int predictedReference =
        group.members().empty() ? plan.routes.front().referenceChannel
                                 : group.members().front().referenceChannel;
    std::vector<std::size_t> predictedMemberIndices;
    std::vector<int> desiredTfIndices;
    for (std::size_t i = 0; i < plan.routes.size(); ++i) {
        if (plan.routes[i].referenceChannel != predictedReference) continue;
        predictedMemberIndices.push_back(i);
        desiredTfIndices.push_back(plan.routes[i].tfIndex);
    }

    if (desiredTfIndices == lastTfIndices) {
        // Same member set, same order -- nothing to rebuild. Not inside any
        // counted publish window (that is publishAverageGroup()'s job
        // alone), but there is no reason to churn membership on every
        // 50 ms publish tick when nothing routing-related has changed.
        return predictedMemberIndices;
    }

    // Every surviving member's trim (record §7: an operator-set dB trim
    // must not reset itself because a DIFFERENT channel elsewhere was
    // reassigned) and the solo selection carry over, matched by tfIndex --
    // the grouping tag a route keeps across a routing-plan rebuild even
    // though its `analysers_` index can move.
    const auto oldMembers = group.members();
    const int oldSolo = group.solo();
    group = AverageGroup{};

    std::vector<std::size_t> memberAnalyserIndices;
    std::vector<int> newTfIndices;
    for (std::size_t i = 0; i < plan.routes.size(); ++i) {
        const auto& route = plan.routes[i];
        double trim = 1.0;
        for (const auto& old : oldMembers) {
            if (old.tfIndex == route.tfIndex) {
                trim = old.trim;
                break;
            }
        }
        // EVERY route is offered to addMember(), including ones the
        // prediction above already expects to lose -- this is the real
        // refusal path (record §6), not the prediction, deciding who
        // becomes a member. A refused route is still represented: it is
        // simply absent from `memberAnalyserIndices`, and
        // `mergeRoutePositions` (below) gives it its own
        // `Membership::ExcludedDifferentReference` summary instead of
        // silently vanishing from the published Snapshot.
        const MemberRefusal refusal = group.addMember(
            route.tfIndex, route.referenceChannel, "TF " + std::to_string(route.tfIndex), trim);
        if (refusal == MemberRefusal::None) {
            memberAnalyserIndices.push_back(i);
            newTfIndices.push_back(route.tfIndex);
        }
    }
    if (oldSolo >= 0) {
        group.setSolo(oldSolo);
    }

    lastTfIndices = std::move(newTfIndices);
    return memberAnalyserIndices;
}

namespace {

/// One `PositionSummary` per `plan.routes` entry, in route order, ALWAYS
/// (station-4 fix F3, record §6): `memberSummaries` (from
/// `AverageGroup::publish()`, via `publishAverageGroup` above) covers only
/// the routes `memberAnalyserIndices` names, in that same ascending order
/// (`syncAverageGroupMembership`'s own guarantee) -- so this is a single
/// merge pass, never a search. A route index NOT in `memberAnalyserIndices`
/// was refused by the real `AverageGroup::addMember` call
/// (`syncAverageGroupMembership`'s own comment) for naming a different
/// reference; it gets a `PositionSummary` carrying only its own identity
/// (`tfIndex`, `name`) and `Membership::ExcludedDifferentReference` -- the
/// same "no data, no crash" shape `AverageGroup::publish()`'s own
/// size-mismatch fallback already uses for a summary with nothing behind
/// it, because this route was never handed to that class as a member in
/// the first place, so there is no `TransferSnapshot` of its own to read a
/// level or a coherence from here.
std::vector<PositionSummary> mergeRoutePositions(const RoutingPlan& plan,
                                                  std::span<const std::size_t> memberAnalyserIndices,
                                                  std::vector<PositionSummary> memberSummaries) {
    std::vector<PositionSummary> merged;
    merged.reserve(plan.routes.size());

    std::size_t memberCursor = 0;
    for (std::size_t routeIndex = 0; routeIndex < plan.routes.size(); ++routeIndex) {
        if (memberCursor < memberAnalyserIndices.size() &&
            memberAnalyserIndices[memberCursor] == routeIndex) {
            merged.push_back(std::move(memberSummaries[memberCursor]));
            ++memberCursor;
            continue;
        }

        PositionSummary excluded;
        excluded.tfIndex = plan.routes[routeIndex].tfIndex;
        excluded.name = "TF " + std::to_string(plan.routes[routeIndex].tfIndex);
        excluded.membership = Membership::ExcludedDifferentReference;
        merged.push_back(std::move(excluded));
    }
    return merged;
}

}  // namespace

AverageGroupPublish publishAverageGroup(const AverageGroup& group,
                                        std::span<const std::size_t> memberAnalyserIndices,
                                        const std::vector<std::unique_ptr<Analyser>>& analysers) {
    std::vector<rta::dsp::TransferSnapshot> positions;
    positions.reserve(memberAnalyserIndices.size());
    for (const std::size_t index : memberAnalyserIndices) {
        positions.push_back(analysers[index]->transferSnapshotForAverage());
    }
    return group.publish(positions);
}

SnapshotPtr buildPublishedSnapshot(std::vector<std::unique_ptr<Analyser>>& analysers,
                                   AverageGroup& group, std::vector<int>& lastTfIndices,
                                   const RoutingPlan& plan, std::uint64_t droppedSamples) {
    // analysers[0] always builds the base Snapshot: its own single-channel
    // spectrum engines are fed by the SAME pushPair() call that feeds its
    // dual-FFT engine whether or not any route exists at all
    // (Analyser::pushPair's own comment), so this is correct for both the
    // plain single-channel path (plan.routes empty) and route 0's own
    // curve when routes exist.
    SnapshotPtr base = analysers[0]->publish(droppedSamples);

    const auto memberAnalyserIndices = syncAverageGroupMembership(group, lastTfIndices, plan);
    if (plan.routes.empty()) {
        return base;
    }

    auto grouped = publishAverageGroup(group, memberAnalyserIndices, analysers);

    auto snapshot = std::make_shared<Snapshot>(*base);
    snapshot->average = std::move(grouped.average);
    snapshot->soloTransfer = std::move(grouped.soloTransfer);
    // Every route gets a summary, member or not (station-4 fix F3) -- see
    // mergeRoutePositions's own comment for why this is a single ordered
    // merge, not a search.
    snapshot->positions =
        mergeRoutePositions(plan, memberAnalyserIndices, std::move(grouped.positions));
    return snapshot;
}

}  // namespace rta::measure
