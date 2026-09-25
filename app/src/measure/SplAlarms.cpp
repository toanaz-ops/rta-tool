// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Lane L6a task W2-B (record docs/dsp/2026-09-16-spl-pro-l6a.md §6, §13 Q5).
#include "measure/SplAlarms.h"

#include <algorithm>

namespace rta::measure {

namespace {

/// One full (never partial) window's Leq, over `blocks[start, start+windowBlocks)`.
std::optional<double> windowLeqAt(std::span<const rta::meter::Block> blocks, double sampleRate,
                                  double referenceOffsetDb, std::uint64_t windowBlocks,
                                  std::size_t start) {
    const auto tail = blocks.subspan(start, windowBlocks);
    return rta::meter::combineBlocks(tail, sampleRate, referenceOffsetDb, windowBlocks).leqDb;
}

}  // namespace

std::optional<double> slidingMaxLeqDb(std::span<const rta::meter::Block> blocks, double sampleRate,
                                      double referenceOffsetDb, std::uint64_t windowBlocks) noexcept {
    if (windowBlocks == 0 || blocks.size() < windowBlocks) return std::nullopt;
    std::optional<double> best;
    // EVERY start position, one block apart -- the sliding policy's whole
    // point (record §13 Q5) is that it does not wait for a grid boundary.
    for (std::size_t start = 0; start + windowBlocks <= blocks.size(); ++start) {
        const auto value = windowLeqAt(blocks, sampleRate, referenceOffsetDb, windowBlocks, start);
        if (value.has_value() && (!best.has_value() || *value > *best)) best = value;
    }
    return best;
}

std::optional<double> consecutiveFixedMaxLeqDb(std::span<const rta::meter::Block> blocks,
                                               double sampleRate, double referenceOffsetDb,
                                               std::uint64_t windowBlocks) noexcept {
    if (windowBlocks == 0 || blocks.size() < windowBlocks) return std::nullopt;
    std::optional<double> best;
    // Grid-aligned at 0: starts are 0, windowBlocks, 2*windowBlocks, ... --
    // a STRICT SUBSET of slidingMaxLeqDb's start positions above, which is
    // the whole reason the sliding maximum can never be smaller (B1).
    for (std::size_t start = 0; start + windowBlocks <= blocks.size(); start += windowBlocks) {
        const auto value = windowLeqAt(blocks, sampleRate, referenceOffsetDb, windowBlocks, start);
        if (value.has_value() && (!best.has_value() || *value > *best)) best = value;
    }
    return best;
}

void SplAlarm::update(std::span<const rta::meter::Block> blocks, double sampleRate,
                      double blockSeconds, double referenceOffsetDb, std::uint64_t currentBlockIndex,
                      SplHistory& history) {
    const std::size_t take = std::min<std::size_t>(spec_.windowBlocks, blocks.size());
    const auto tail = blocks.subspan(blocks.size() - take, take);
    const auto windowed =
        rta::meter::combineBlocks(tail, sampleRate, referenceOffsetDb, spec_.windowBlocks);

    const auto transition = latch_.update(windowed, spec_.limitDb);

    headroomDb_ = std::nullopt;
    if (windowed.leqDb.has_value()) {
        const double windowSeconds = static_cast<double>(spec_.windowBlocks) * blockSeconds;
        headroomDb_ = rta::meter::headroomDb(windowSeconds, windowed.seconds, *windowed.leqDb,
                                            spec_.limitDb);
    }

    if (transition != rta::meter::AlarmLatch::Transition::None) {
        state_ = (transition == rta::meter::AlarmLatch::Transition::Fired) ? SplAlarmState::Fired
                                                                          : SplAlarmState::Clear;
        sinceBlock_ = currentBlockIndex;

        SplMarker marker;
        marker.blockIndex = currentBlockIndex;
        marker.kind = SplMarkerKind::Alarm;
        marker.direction = (transition == rta::meter::AlarmLatch::Transition::Fired) ? 1 : -1;
        marker.quantity = spec_.metricId;
        marker.window = spec_.windowBlocks;
        // BITWISE the same value the latch just compared -- B3's own wording.
        // `windowed.leqDb` is guaranteed present here: `AlarmLatch::update`
        // returns `None` (never a transition) for an absent window, so
        // reaching this branch means the comparison happened against a real
        // value.
        marker.value = *windowed.leqDb;
        history.addMarker(std::move(marker));
    }
}

SplAlarmReport SplAlarm::report() const {
    SplAlarmReport out;
    out.metricId = spec_.metricId;
    out.limitDb = spec_.limitDb;
    out.windowBlocks = spec_.windowBlocks;
    out.state = state_;
    out.sinceBlock = sinceBlock_;
    out.headroomDb = headroomDb_;
    return out;
}

SplAlarms::SplAlarms(std::vector<SplAlarmSpec> specs) {
    alarms_.reserve(specs.size());
    for (auto& spec : specs) alarms_.emplace_back(std::move(spec));
}

void SplAlarms::update(std::span<const rta::meter::Block> blocks, double sampleRate,
                       double blockSeconds, double referenceOffsetDb,
                       std::uint64_t currentBlockIndex, SplHistory& history) {
    for (auto& alarm : alarms_) {
        alarm.update(blocks, sampleRate, blockSeconds, referenceOffsetDb, currentBlockIndex, history);
    }
}

std::vector<SplAlarmReport> SplAlarms::reports() const {
    std::vector<SplAlarmReport> out;
    out.reserve(alarms_.size());
    for (const auto& alarm : alarms_) out.push_back(alarm.report());
    return out;
}

}  // namespace rta::measure
