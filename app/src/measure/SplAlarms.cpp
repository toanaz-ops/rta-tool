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
    const bool windowFull = take == spec_.windowBlocks;

    if (windowed.leqDb.has_value()) valueDb_ = static_cast<float>(*windowed.leqDb);

    // headroomDb (record §15 A6, fix round item 4). The identity itself
    // (Alarm.h) never changes; only WHICH (t, L_t) pair it is fed does, and
    // that pair is continuous across the fill->full transition -- neither
    // side introduces a new constant.
    //
    //   FILLING (window not yet full): t = elapsed seconds, T = window
    //   seconds, L_t = the Leq of whatever partial tail exists -- this is
    //   asking "what can the block that completes THIS window be".
    //
    //   FULL: t = T - Delta (Delta = one block's duration), L_t = the Leq of
    //   the MOST RECENT windowBlocks-1 blocks -- once the window is full,
    //   there is no "next slot in this window" left to fill, so the question
    //   becomes "what can the FIRST block of the NEXT sliding window be":
    //   that window drops the current oldest block and keeps the current
    //   newest windowBlocks-1, which is exactly `tail` with its first (oldest)
    //   entry removed. Recomputed as a fresh energy sum over those blocks
    //   (record §3's own "recomputed over current membership" rule) -- never
    //   carried from a running total.
    headroomDb_ = std::nullopt;
    if (!windowFull) {
        if (windowed.leqDb.has_value()) {
            const double windowSeconds = static_cast<double>(spec_.windowBlocks) * blockSeconds;
            headroomDb_ = rta::meter::headroomDb(windowSeconds, windowed.seconds, *windowed.leqDb,
                                                spec_.limitDb);
        }
    } else if (spec_.windowBlocks > 0) {
        const auto recent = tail.subspan(1, spec_.windowBlocks - 1);
        const auto recentResult = rta::meter::combineBlocks(recent, sampleRate, referenceOffsetDb,
                                                            spec_.windowBlocks - 1);
        if (recentResult.leqDb.has_value()) {
            const double windowSeconds = static_cast<double>(spec_.windowBlocks) * blockSeconds;
            const double tSeconds = windowSeconds - blockSeconds;
            headroomDb_ = rta::meter::headroomDb(windowSeconds, tSeconds, *recentResult.leqDb,
                                                spec_.limitDb);
        }
    }

    // The latch is NOT evaluated until the window actually holds
    // `windowBlocks` blocks (PR #26 fix round item 3). `combineBlocks` over a
    // PARTIAL tail happily returns a real `leqDb` -- that is what §9's
    // `bufferFill` is for -- so without this gate a single loud block against
    // a 900-block window fired immediately. `AlarmLatch::update`'s own
    // contract (Alarm.h) is "an ABSENT leqDb is not compared at all"; a
    // still-filling window is exactly that case, so it is fed `std::nullopt`
    // rather than a premature partial value, and `state_` simply stays at
    // whatever it already was (`Clear`, its constructed default, until the
    // window first fills) with no marker written.
    const rta::meter::WindowResult forLatch = windowFull ? windowed : rta::meter::WindowResult{};
    const auto transition = latch_.update(forLatch, spec_.limitDb);

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

SplAlarmReading SplAlarm::report() const {
    SplAlarmReading out;
    out.metricId = spec_.metricId;
    out.limitDb = spec_.limitDb;
    out.valueDb = valueDb_;
    out.headroomDb = headroomDb_;
    out.state = state_;
    out.windowBlocks = spec_.windowBlocks;
    out.sinceBlock = sinceBlock_;
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

std::vector<SplAlarmReading> SplAlarms::reports() const {
    std::vector<SplAlarmReading> out;
    out.reserve(alarms_.size());
    for (const auto& alarm : alarms_) out.push_back(alarm.report());
    return out;
}

}  // namespace rta::measure
