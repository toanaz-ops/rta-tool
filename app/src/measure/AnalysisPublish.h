// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Split out of AnalysisThread's own publishIfDue() (task F2,
// docs/plans/2026-09-06-L6b-impl-plan.md task B3's follow-up) to keep that
// file under the project's line cap AND to make the wiring itself testable
// with RTA_BUILD_APP=OFF -- free functions over rta::measure::Analyser and
// rta::measure::AverageGroup, neither of which needs JUCE, so this is the
// EXACT code AnalysisThread::publishIfDue() calls, exercised directly by
// real Analysers with no bus and no thread in the path (see
// app/tests/test_analysis_publish.cpp).
#pragma once

#include "measure/Analyser.h"
#include "measure/AverageGroup.h"
#include "measure/RoutingPlan.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace rta::measure {

/// Keeps `group`'s membership in step with `plan.routes`. Record §6: "two
/// systems measured against two references are two groups, not one
/// average" -- this app runs exactly ONE live group per AnalysisThread
/// today, so when `plan` names more than one distinct reference, only the
/// routes sharing the group's established reference channel become
/// members. EVERY route -- member or not -- is now fed to
/// `AverageGroup::addMember` (station-4 fix F3: a route naming a different
/// reference must be REFUSED by that real call, not filtered out before it
/// ever runs, which had made `MemberRefusal::DifferentReference`
/// unreachable in production). A refused route is not lost: it is still
/// represented in the published `Snapshot` by `mergeRoutePositions`
/// (AnalysisPublish.cpp), which is what turns "excluded from this function's
/// return value" into a visible `Membership::ExcludedDifferentReference`
/// summary instead of no summary at all.
///
/// `lastTfIndices` is the caller's own memory of what was last synced --
/// pass the SAME vector back on every call. The (expensive) group rebuild
/// is skipped when the PREDICTED member set has not changed since last time
/// -- a cheap, non-mutating restatement of `AverageGroup::addMember`'s own
/// one-line rule against the group's current state, used only to decide
/// whether to rebuild, never to decide who is actually a member (that
/// decision always comes from a real `addMember` call inside the rebuild).
/// A surviving member's trim and the group's solo selection carry over
/// unchanged across a rebuild, matched by `TransferRoute::tfIndex`, so a
/// routing change on an unrelated channel never resets a trim the operator
/// already set.
///
/// Returns the position, in `plan.routes` (the SAME order `analysers_` is
/// indexed by -- AnalysisThread's own class comment), of each surviving
/// member, in the order `group.members()` now holds -- what the caller
/// needs in order to read each member's TransferSnapshot from the right
/// `Analyser`. Ascending by construction (both the prediction and the real
/// rebuild walk `plan.routes` in order), which is what lets
/// `mergeRoutePositions` zip it against `plan.routes` in one pass.
[[nodiscard]] std::vector<std::size_t> syncAverageGroupMembership(
    AverageGroup& group, std::vector<int>& lastTfIndices, const RoutingPlan& plan);

/// One publish's worth of the routed (non-empty `plan.routes`) path:
/// gathers every member's `TransferSnapshot` through
/// `Analyser::transferSnapshotForAverage()` -- which reads as "excluded"
/// rather than throwing when a position has not engaged its dual engine yet
/// -- in the order `memberAnalyserIndices` names, and returns
/// `group.publish()`'s own result unchanged.
///
/// This is the ONE call the T12 counting-allocator test (record §6,
/// app/tests/test_average_group.cpp) measures: `AverageGroup::publish()`
/// allocates only `O(bins)` plus one small `PositionSummary` per member,
/// never a full per-position curve for every member (SpatialAverage.cpp's
/// own allocation is sized by bin count, never by position count) -- the
/// design answer record §6 gives to the 2.21 MB-per-position-per-publish
/// churn a naive N-Analyser publish would otherwise cost.
[[nodiscard]] AverageGroupPublish publishAverageGroup(
    const AverageGroup& group, std::span<const std::size_t> memberAnalyserIndices,
    const std::vector<std::unique_ptr<Analyser>>& analysers);

/// The whole of `AnalysisThread::publishIfDue()`'s own orchestration
/// (station-4 fix pass, task F2), pulled out to a free function so that file
/// stays under the project's line cap and so this exact logic is testable
/// with no bus and no thread in the path. `analysers[0]` always builds the
/// BASE `Snapshot` -- its bands/spectrumDb/transfer/mtw describe route 0
/// (or the plain single-channel path when `plan.routes` is empty) exactly
/// as `Analyser::publish()` on its own already did before this task.
///
/// When `plan.routes` is non-empty, `group`'s membership is synced (see
/// `syncAverageGroupMembership`'s own comment) and its publish is folded
/// into a COPY of the base `Snapshot` -- `average`, `positions` and
/// `soloTransfer` are the only three fields this adds. That copy is a
/// fixed cost, sized by bins, the SAME every publish regardless of N; it is
/// deliberately not the quantity T12 bounds (`publishAverageGroup` above
/// is).
///
/// `positions` is `mergeRoutePositions`'s own result (AnalysisPublish.cpp,
/// station-4 fix F3): one `PositionSummary` per `plan.routes` entry, in
/// route order, ALWAYS -- a route `AverageGroup::addMember` refused for
/// naming a different reference gets a summary too, marked
/// `Membership::ExcludedDifferentReference`, rather than being missing from
/// the Snapshot entirely.
[[nodiscard]] SnapshotPtr buildPublishedSnapshot(std::vector<std::unique_ptr<Analyser>>& analysers,
                                                 AverageGroup& group,
                                                 std::vector<int>& lastTfIndices,
                                                 const RoutingPlan& plan,
                                                 std::uint64_t droppedSamples);

}  // namespace rta::measure
