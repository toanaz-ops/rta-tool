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
    // Only route POSITIONS below kMaxTransferFunctions can ever be members:
    // there are exactly that many Analysers, indexed by position, and a route
    // past the cap has neither an Analyser nor audio (AnalysisThread::drainPaired
    // stops at the same bound). Capping the PREDICTION too keeps it in step with
    // the real rebuild below -- otherwise a plan with more than the cap of
    // shared-reference routes would predict a member set the rebuild can never
    // match, forcing a needless rebuild on every 50 ms publish tick.
    const std::size_t cap = static_cast<std::size_t>(kMaxTransferFunctions);
    std::vector<std::size_t> predictedMemberIndices;
    std::vector<int> desiredTfIndices;
    for (std::size_t i = 0; i < plan.routes.size() && i < cap; ++i) {
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
    for (std::size_t i = 0; i < plan.routes.size() && i < cap; ++i) {
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
        // Position tells us WHY this route is not a member, with no ambiguity:
        // a route at or past kMaxTransferFunctions has no Analyser at all
        // (ExcludedOverCapacity), while a route below the cap that is still not
        // a member can only have been refused by AverageGroup::addMember for
        // naming a different reference -- that is addMember's one refusal.
        excluded.membership = routeIndex >= static_cast<std::size_t>(kMaxTransferFunctions)
                                  ? Membership::ExcludedOverCapacity
                                  : Membership::ExcludedDifferentReference;
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

std::optional<SplBlockView> buildSplBlockView(const SplPublishInput& input) {
    // Absence, in both of its forms. Nothing is logging (no config), or a
    // logging session has not closed its first block yet -- and neither may
    // publish a zeroed block that reads as a measurement
    // (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
    if (input.config == nullptr || !input.latestBlock.has_value()) {
        return std::nullopt;
    }
    const SplConfig& config = *input.config;
    const rta::meter::Block& latest = *input.latestBlock;

    SplBlockView view;
    view.blockIndex = latest.blockIndex;
    view.blockSamples = latest.blockSamples;
    view.sampleRate = input.sampleRate;
    view.referenceOffsetDb = config.referenceOffsetDb;
    view.calibrated = config.calibrated;
    view.flags = latest.flags;
    view.droppedSamples = latest.droppedSamples;
    // The block stores un-offset dB so a calibration offset discovered later
    // can be applied to a log without rewriting it; the offset is added here,
    // at the publish, exactly once.
    view.maxFastDb = static_cast<float>(latest.maxFastDb + config.referenceOffsetDb);
    view.maxSlowDb = static_cast<float>(latest.maxSlowDb + config.referenceOffsetDb);
    view.peakCDb = static_cast<float>(latest.peakDb + config.referenceOffsetDb);

    view.refusedMetrics = static_cast<std::uint32_t>(input.refusedMetrics);
    // Round-4 items 3/4: both live counters/flags on the SESSION or the
    // A-weighted METER, never on `SplChannelState`, so both are filled here
    // -- the same spot `refusedMetrics` just above already is -- rather than
    // inside `SplChannelState::fillPublish`.
    view.lnTicksOverflowed = static_cast<std::uint32_t>(input.overflowedLnTicks);
    view.blockSecondsBelowRecommendedFloor = input.blockSecondsTooSmall;
    view.metrics.reserve(config.metrics.size());
    for (std::size_t i = 0; i < config.metrics.size(); ++i) {
        const SplMetricSpec& spec = config.metrics[i];
        const std::uint64_t windowBlocks = spec.windowBlocks == 0 ? 1 : spec.windowBlocks;

        // THIS METRIC'S OWN window. `SplMeter` runs one weighting per instance
        // (W0-B), so an A-weighted metric and a C-weighted one are averaged
        // over different chains and `metricWindows` is what says which.
        //
        // NO PER-METRIC FALLBACK once `metricWindows` is non-empty (PR #17
        // verifier defect 1). An EMPTY `metricWindows` means the caller
        // supplied none at all -- the single-weighting case W0-C's own
        // fixtures use -- and then every metric reads the shared `window`. But
        // a caller that supplied SOME and ran out has said nothing about the
        // rest, and reading `window` for those was how a C-weighted metric
        // came to publish A-weighted numbers under a C label, 18.8 dB wrong.
        // A row nobody filled is an EMPTY span, which `combineBlocks` turns
        // into an absent Leq: the reading floors instead of lying.
        const bool perMetric = !input.metricWindows.empty();
        const auto source =
            perMetric ? (i < input.metricWindows.size() ? input.metricWindows[i]
                                                        : std::span<const rta::meter::Block>{})
                      : input.window;

        // The LAST windowBlocks of the buffer. A window longer than the
        // buffer takes the whole buffer and says so through bufferFill --
        // never a shorter answer presented as a full one (record §9).
        const std::size_t take = std::min(static_cast<std::size_t>(windowBlocks), source.size());
        const auto tail = source.subspan(source.size() - take, take);
        const auto result = rta::meter::combineBlocks(tail, input.sampleRate,
                                                      config.referenceOffsetDb, windowBlocks);

        SplMetricReading reading;
        reading.id = spec.id;
        reading.valueDb = static_cast<float>(result.leqDb.value_or(kLevelFloorDb));
        reading.leqBufferFill = static_cast<float>(result.bufferFill);
        view.metrics.push_back(std::move(reading));
    }

    // `alarms`, `dosePercent`, `doseProjected` and `lnDb` come from the
    // channel's own accumulated state (lane L6a task W2-E1: SplHistory,
    // SplAlarms, LevelHistogram and the two Dose accumulators, fed once per
    // closed block by AnalysisThread::feedSpl -- never recomputed here).
    // `channelState` is nullptr only when nothing is logging on this
    // channel, which `input.config == nullptr` above already returns absent
    // for -- so reaching this line with a null `channelState` would itself
    // be a wiring defect, not an expected state, and every slot simply stays
    // at `view`'s own default (empty vector, absent optional) rather than a
    // placeholder zero (memory/a-placeholder-for-an-absent-result-erases-
    // its-state.md).
    if (input.channelState != nullptr) {
        input.channelState->fillPublish(view);
    }
    // W2-E2a: always copied, independent of channelState -- a queue can
    // overflow even on a channel whose SplChannelState allocation succeeded,
    // and the two failure modes are unrelated (one is a full ring under disk
    // I/O pressure, the other is absent-because-nothing-is-logging, already
    // handled by the early return above).
    view.logDroppedBlocks = static_cast<std::uint32_t>(input.logDroppedBlocks);
    return view;
}

SnapshotPtr buildPublishedSnapshot(std::vector<std::unique_ptr<Analyser>>& analysers,
                                   AverageGroup& group, std::vector<int>& lastTfIndices,
                                   const RoutingPlan& plan, std::uint64_t droppedSamples,
                                   const SplPublishInput* spl) {
    // analysers[0] always builds the base Snapshot: its own single-channel
    // spectrum engines are fed by the SAME pushPair() call that feeds its
    // dual-FFT engine whether or not any route exists at all
    // (Analyser::pushPair's own comment), so this is correct for both the
    // plain single-channel path (plan.routes empty) and route 0's own
    // curve when routes exist.
    SnapshotPtr base = analysers[0]->publish(droppedSamples);

    // The SPL fold. Built once, before the routing branch below, because it
    // does not depend on routing at all: a session with no reference and no
    // route logs SPL (research §C4), which is the whole point of W0-D.
    auto splView = (spl != nullptr) ? buildSplBlockView(*spl) : std::nullopt;

    const auto memberAnalyserIndices = syncAverageGroupMembership(group, lastTfIndices, plan);
    if (plan.routes.empty()) {
        if (!splView.has_value()) {
            return base;
        }
        // One field to add, so one copy -- the same fixed, bins-sized cost
        // the routed branch below already pays, and paid only when something
        // is actually logging.
        auto plainWithSpl = std::make_shared<Snapshot>(*base);
        plainWithSpl->spl = std::move(splView);
        return plainWithSpl;
    }

    auto grouped = publishAverageGroup(group, memberAnalyserIndices, analysers);

    auto snapshot = std::make_shared<Snapshot>(*base);
    snapshot->spl = std::move(splView);
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
