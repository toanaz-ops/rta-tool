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

    // headroomDb (record §15 A6, fix round item 4; exclusion rule, fix round
    // item 3). The identity itself (Alarm.h) never changes; only WHICH (t,
    // T, L_t) triple it is fed does, and that triple is continuous across
    // the fill->full transition -- neither side introduces a new constant.
    // `T` in BOTH branches is, by construction, THE DIVISOR THE NEXT
    // COMPLIANCE WINDOW WILL ACTUALLY HAVE: what the compliance Leq itself
    // divides by is combineBlocks' MEASURED seconds, which drops excluded
    // blocks, so an exclusion must shrink `T` exactly as far as it shrinks
    // `t` -- never one without the other, or the identity is answering a
    // question about a window that cannot occur.
    //
    //   FILLING (window not yet full): t = elapsed MEASURED seconds
    //   (`windowed.seconds`, already exclusion-aware), T = the nominal
    //   window seconds LESS whatever has already been excluded from it --
    //   the divisor the first full window will have if nothing more is
    //   excluded. L_t = the Leq of whatever partial tail exists -- this is
    //   asking "what can the block that completes THIS window be".
    //
    //   FULL: once the window is full, there is no "next slot in this
    //   window" left to fill, so the question becomes "what can the FIRST
    //   block of the NEXT sliding window be": that window drops the current
    //   oldest block and keeps the current newest windowBlocks-1, which is
    //   exactly `tail` with its first (oldest) entry removed. Recomputed as
    //   a fresh energy sum over those blocks (record §3's own "recomputed
    //   over current membership" rule) -- never carried from a running
    //   total. `s` = combineBlocks' measured seconds of that kept span (so
    //   an excluded block among them drops out of `s` too), and `T = s +
    //   Delta`, Delta being one block's duration: the next window is
    //   exactly those kept blocks plus one new one. With no exclusions,
    //   `s == windowBlocks-1 blocks worth of seconds` and this reduces to
    //   the original `t = T - Delta` unconditionally.
    //
    //   An EMPTY recent span (windowBlocks == 1, the SplAlarmSpec default)
    //   falls out of the same formula for free: `s = 0`, `T = Delta`, and at
    //   t = 0 `spent = t * 10^(Lt/10)` is 0 regardless of L_t, so the answer
    //   is exactly limitDb without a separate special case.
    headroomDb_ = std::nullopt;
    if (!windowFull) {
        if (windowed.leqDb.has_value()) {
            const double windowSeconds = static_cast<double>(spec_.windowBlocks) * blockSeconds;
            const double excludedSecondsSoFar =
                static_cast<double>(windowed.excludedBlocks) * blockSeconds;
            const double T = windowSeconds - excludedSecondsSoFar;
            headroomDb_ = rta::meter::headroomDb(T, windowed.seconds, *windowed.leqDb, spec_.limitDb);
        }
    } else if (spec_.windowBlocks > 0) {
        const auto recent = tail.subspan(1, spec_.windowBlocks - 1);
        const auto recentResult = rta::meter::combineBlocks(
            recent, sampleRate, referenceOffsetDb, static_cast<std::uint64_t>(recent.size()));
        // `recentResult.leqDb` is absent only when `recent` has no samples at
        // all (empty span, or every block in it excluded) -- in that case
        // `recentResult.seconds` is also exactly 0, so `spent` below is 0
        // regardless of the placeholder 0.0 fed as L_t.
        const double sRecent = recentResult.seconds;
        const double recentLeqDb = recentResult.leqDb.value_or(0.0);
        headroomDb_ = rta::meter::headroomDb(sRecent + blockSeconds, sRecent, recentLeqDb,
                                            spec_.limitDb);
    }

    // The latch is NOT evaluated until the window actually holds
    // `windowBlocks` blocks (PR #26 fix round item 3). `combineBlocks` over a
    // PARTIAL tail happily returns a real `leqDb` -- that is what §9's
    // `bufferFill` is for -- so without this gate a single loud block against
    // a 900-block window fired immediately. `AlarmLatch::update`'s own
    // contract (Alarm.h) is "an ABSENT leqDb is not compared at all"; a
    // still-filling window is exactly that case, so it is fed `std::nullopt`
    // rather than a premature partial value, and `state_` simply stays at
    // whatever it already was (`Filling`, its constructed default, until the
    // window first fills) with no marker written.
    const rta::meter::WindowResult forLatch = windowFull ? windowed : rta::meter::WindowResult{};
    const auto transition = latch_.update(forLatch, spec_.limitDb);

    // Filling -> {Clear, Fired} happens exactly once, the instant the window
    // first holds windowBlocks blocks (record §15 A6, round 4). This is
    // SEPARATE from the `transition != None` branch below: AlarmLatch's
    // `active_` starts false, so a COMPLIANT first window matches it
    // (over == active_ == false) and AlarmLatch reports Transition::None --
    // the identical value it reports for "nothing changed" on every later
    // window. Left alone, `state_` would stay at its Filling placeholder
    // forever on a first window that never exceeds the limit. Filling->Clear
    // writes NO marker here (it is not an alarm transition, B3's own
    // wording); Filling->Fired's marker is the ordinary Fired transition
    // handled below, unchanged.
    if (windowFull && !windowEverFull_) {
        windowEverFull_ = true;
        if (transition == rta::meter::AlarmLatch::Transition::None) {
            state_ = SplAlarmState::Clear;
        }
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
