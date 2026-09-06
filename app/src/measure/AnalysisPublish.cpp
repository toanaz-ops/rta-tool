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

    // Record §6: a route naming a DIFFERENT reference than the first route's
    // is a different group, not built here yet (this function's own header
    // comment states the limitation) -- filter to the routes that share
    // plan.routes.front()'s reference before touching `group` at all.
    const int firstReference = plan.routes.front().referenceChannel;
    std::vector<std::size_t> memberAnalyserIndices;
    std::vector<int> desiredTfIndices;
    for (std::size_t i = 0; i < plan.routes.size(); ++i) {
        if (plan.routes[i].referenceChannel != firstReference) continue;
        memberAnalyserIndices.push_back(i);
        desiredTfIndices.push_back(plan.routes[i].tfIndex);
    }

    if (desiredTfIndices == lastTfIndices) {
        // Same member set, same order -- nothing to rebuild. Not inside any
        // counted publish window (that is publishAverageGroup()'s job
        // alone), but there is no reason to churn membership on every
        // 50 ms publish tick when nothing routing-related has changed.
        return memberAnalyserIndices;
    }

    // Every surviving member's trim (record §7: an operator-set dB trim
    // must not reset itself because a DIFFERENT channel elsewhere was
    // reassigned) and the solo selection carry over, matched by tfIndex --
    // the grouping tag a route keeps across a routing-plan rebuild even
    // though its `analysers_` index can move.
    const auto oldMembers = group.members();
    const int oldSolo = group.solo();
    group = AverageGroup{};

    for (const std::size_t analyserIndex : memberAnalyserIndices) {
        const auto& route = plan.routes[analyserIndex];
        double trim = 1.0;
        for (const auto& old : oldMembers) {
            if (old.tfIndex == route.tfIndex) {
                trim = old.trim;
                break;
            }
        }
        // Never refused: every route gathered above already agrees with
        // `firstReference` by construction, and `addMember`'s only refusal
        // is a reference mismatch (record §6).
        (void)group.addMember(route.tfIndex, route.referenceChannel,
                              "TF " + std::to_string(route.tfIndex), trim);
    }
    if (oldSolo >= 0) {
        group.setSolo(oldSolo);
    }

    lastTfIndices = std::move(desiredTfIndices);
    return memberAnalyserIndices;
}

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
    snapshot->positions = std::move(grouped.positions);
    snapshot->soloTransfer = std::move(grouped.soloTransfer);
    return snapshot;
}

}  // namespace rta::measure
