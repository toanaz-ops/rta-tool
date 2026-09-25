// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Lane L6a task W2-E1. Fix round 2026-09-25 -- see SplChannelState.h.
#include "measure/SplChannelState.h"

#include <algorithm>
#include <utility>

namespace rta::measure {

std::span<const rta::meter::Block> windowAtClose(std::span<const rta::meter::Block> closed,
                                                  std::span<const rta::meter::Block> fullWindow,
                                                  std::size_t i) noexcept {
    if (i >= closed.size()) return {};
    if (closed.size() <= fullWindow.size()) {
        const std::size_t trim = closed.size() - 1 - i;
        return fullWindow.first(fullWindow.size() - trim);
    }
    const std::size_t count = std::min(fullWindow.size(), i + 1);
    return closed.subspan(i + 1 - count, count);
}

namespace {

/// Partitions `config.alarms` by the weighting of the metric each alarm's
/// `metricId` names, in first-encountered order -- the construction-time
/// half of "every consumer reads the chain its own definition names"
/// (SplChannelState.h's own class comment). An alarm naming no configured
/// metric lands in the group with `weighting == std::nullopt`, which
/// `onBlockClosed` never feeds (that group's `SplAlarmReading` stays at its
/// constructed default: `Filling`, no headroom).
std::vector<SplChannelState::AlarmGroup> buildAlarmGroups(const SplConfig& config,
                                                           std::vector<std::size_t>& outPublishOrder) {
    struct Bucket {
        std::optional<rta::dsp::WeightingType> weighting;
        std::vector<SplAlarmSpec> specs;
        // PR #29 round-3 step 3: parallel to `specs`, resolved in THIS SAME
        // loop -- the metric's INDEX, not its id, because two 32+ char ids
        // sharing a 31-char prefix collide under MarkerQuantity's
        // truncation, and a marker's real identity must survive that
        // (SplHistory.h's own SplMarker::metricIndex comment).
        std::vector<std::optional<std::size_t>> metricIndices;
        // PR #29 round-3 step 5: parallel to `specs` too -- each spec's own
        // ORIGINAL index in `config.alarms`, before this partition scrambled
        // it (SplChannelState.h's own `alarmPublishOrder_` comment).
        std::vector<std::size_t> configIndices;
    };
    std::vector<Bucket> buckets;
    for (std::size_t configIndex = 0; configIndex < config.alarms.size(); ++configIndex) {
        const SplAlarmSpec& spec = config.alarms[configIndex];
        std::optional<rta::dsp::WeightingType> weighting;
        std::optional<std::size_t> metricIndex;
        for (std::size_t i = 0; i < config.metrics.size(); ++i) {
            if (config.metrics[i].id == spec.metricId) {
                weighting = config.metrics[i].weighting;
                metricIndex = i;
                break;
            }
        }
        auto it = std::find_if(buckets.begin(), buckets.end(),
                               [&](const Bucket& b) { return b.weighting == weighting; });
        if (it == buckets.end()) {
            buckets.push_back(Bucket{weighting, {}, {}, {}});
            it = std::prev(buckets.end());
        }
        it->specs.push_back(spec);
        it->metricIndices.push_back(metricIndex);
        it->configIndices.push_back(configIndex);
    }

    std::vector<SplChannelState::AlarmGroup> groups;
    groups.reserve(buckets.size());
    outPublishOrder.clear();
    outPublishOrder.reserve(config.alarms.size());
    for (auto& bucket : buckets) {
        for (const std::size_t configIndex : bucket.configIndices) {
            outPublishOrder.push_back(configIndex);
        }
        groups.push_back(SplChannelState::AlarmGroup{
            bucket.weighting, SplAlarms(std::move(bucket.specs), std::move(bucket.metricIndices))});
    }
    return groups;
}

}  // namespace

SplChannelState::SplChannelState(const SplConfig& config, double sampleRate)
    : sampleRate_(sampleRate)
    , blockSeconds_(config.blockSeconds)
    , referenceOffsetDb_(config.referenceOffsetDb)
    , lnPercents_(config.lnPercents)
    , history_(SplHistory::capacityBlocks(config.logSpanSeconds, config.blockSeconds))
    , lnHistogram_(config.histogramBaseDb())
    , dose_{{rta::meter::Dose(config.dose[0]), rta::meter::Dose(config.dose[1])}} {
    // Assigned in the body rather than the initialiser list: buildAlarmGroups
    // fills alarmPublishOrder_ as an out-parameter ALONGSIDE building
    // alarmGroups_ itself (PR #29 round-3 step 5), and an initialiser-list
    // entry for alarmGroups_ cannot safely reference a sibling member that
    // is not guaranteed constructed yet under declaration order. Still ONE
    // allocation event, still at construction, still never touched again.
    alarmGroups_ = buildAlarmGroups(config, alarmPublishOrder_);
}

void SplChannelState::onBlockClosed(std::span<const ChainBlockAtClose> chains) {
    if (chains.empty()) return;

    // History: the FIRST configured chain, unchanged (not the fix round's
    // subject -- a generic per-channel display ring, not tied to one
    // metric's weighting).
    history_.push(chains.front().block);

    // Dose + Ln: the A-weighted chain, always present now that
    // SplSession::start() auto-creates it (fix round 2026-09-25). Still
    // guarded defensively -- a caller driving this class directly (as
    // test_spl_channel_state.cpp does) is not required to supply one.
    const ChainBlockAtClose* aChain = nullptr;
    for (const auto& c : chains) {
        if (c.weighting == rta::dsp::WeightingType::A) {
            aChain = &c;
            break;
        }
    }
    if (aChain != nullptr && !rta::meter::hasFlag(aChain->block.flags,
                                                   rta::meter::BlockFlag::CalibrationInvalid)) {
        // Ln no longer comes from the closed block at all (fix round
        // 2026-09-25) -- see `feedLnTicks`, called once per HOP by
        // `AnalysisThread::feedSpl`, never here.

        // Dose (record §7): the block's own broadband Leq, offset-applied --
        // combineBlocks over a single-block "window" is exactly that,
        // reusing the ONE identity rather than re-deriving 10*log10 here.
        const auto single = std::span<const rta::meter::Block>(&aChain->block, 1);
        const auto result =
            rta::meter::combineBlocks(single, sampleRate_, referenceOffsetDb_, 1);
        if (result.leqDb.has_value()) {
            for (auto& dose : dose_) dose.addBlock(*result.leqDb, result.seconds);
        }
    }
    // No A-weighted chain in `chains` at all: dose_ and lnHistogram_ simply
    // stay exactly as constructed (never fed), so fillPublish's own
    // absence gates below leave them ABSENT.

    const std::uint64_t currentBlockIndex = chains.front().block.blockIndex;
    for (auto& group : alarmGroups_) {
        if (!group.weighting.has_value()) continue;  // no configured metric named this alarm
        for (const auto& c : chains) {
            if (c.weighting == *group.weighting) {
                group.alarms.update(c.windowThroughThisBlock, sampleRate_, blockSeconds_,
                                    referenceOffsetDb_, currentBlockIndex, history_);
                break;
            }
        }
    }
}

void SplChannelState::feedLnTicks(std::span<const double> aChainTickLevelsDb) {
    for (double raw : aChainTickLevelsDb) lnHistogram_.add(raw + referenceOffsetDb_);
}

void SplChannelState::fillPublish(SplBlockView& view) const {
    // PR #29 round-3 step 4: overflowedMarkers() already existed on
    // SplHistory; nothing read it. Published unconditionally -- it is a
    // plain uint64->uint32 count, never absent the way an alarm/dose/Ln
    // reading can be, so there is no "not yet produced" state to preserve.
    view.markersOverflowed = static_cast<std::uint32_t>(history_.overflowedMarkers());

    // PR #29 round-3 step 5: gather every group's own readings in the
    // internal (weighting-partitioned) order first -- unchanged from
    // before -- then place each one back at its ORIGINAL `config.alarms`
    // position via `alarmPublishOrder_`, so the published order always
    // matches configuration order regardless of how the routing partition
    // grouped them. `alarmPublishOrder_` is a permutation of
    // `[0, flattened.size())` by construction (`buildAlarmGroups` visits
    // every configured alarm exactly once), so every slot below is written.
    std::vector<SplAlarmReading> flattened;
    for (const auto& group : alarmGroups_) {
        const auto reports = group.alarms.reports();
        flattened.insert(flattened.end(), reports.begin(), reports.end());
    }
    view.alarms.assign(flattened.size(), SplAlarmReading{});
    for (std::size_t i = 0; i < flattened.size() && i < alarmPublishOrder_.size(); ++i) {
        const std::size_t configIndex = alarmPublishOrder_[i];
        if (configIndex < view.alarms.size()) view.alarms[configIndex] = std::move(flattened[i]);
    }

    for (std::size_t i = 0; i < dose_.size(); ++i) {
        // ABSENT until this accumulator has actually seen a block -- a fresh
        // Dose's percent() reads 0.0 %, which is indistinguishable from "no
        // exposure over an elapsed session" unless elapsedSeconds() is
        // checked first (memory/a-placeholder-for-an-absent-result-erases-
        // its-state.md; record §9's own "a zero dose reads as measured").
        if (dose_[i].elapsedSeconds() > 0.0) {
            view.dosePercent[i] = dose_[i].percent();
            view.doseProjected[i] = dose_[i].projectedPercent();
        }
    }

    for (std::size_t i = 0; i < lnPercents_.size(); ++i) {
        view.lnDb[i] = lnHistogram_.percentileDb(lnPercents_[i]);
    }
}

}  // namespace rta::measure
