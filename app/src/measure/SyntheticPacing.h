// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
#pragma once

#include <cmath>

namespace rta::measure {

/// What `SyntheticInput` does after pushing one block: when the NEXT block is due,
/// and how long to sleep first (0 = push at once).
struct SyntheticPace {
    double dueMs;
    int waitMs;
};

/// Deadline pacing for the synthetic feed. The loop used to `wait(blockMs)` after each
/// block. A sleep never returns early and on Windows it returns up to a timer tick
/// (about 15.6 ms) late, so every block cost blockMs + lateness and the "real time"
/// feed ran at 0.87x unloaded and 0.25x on a saturated box (measured: 11 s of audio in
/// 44 s of wall clock). Everything that waits in WALL-CLOCK time for a feed that is
/// paced in LOST time -- the sweep's capture timeout, ALIGN's dwell timeout -- then
/// expired on a healthy run.
///
/// Fix: each block is due `blockMs` after the PREVIOUS DEADLINE, not after the previous
/// wake-up, so lateness is repaid by pushing the next blocks back to back and the
/// long-run rate is exactly 1.0x whenever the CPU can supply it. A lag beyond
/// `maxLagMs` (the thread was descheduled for that long) re-anchors the deadline
/// instead of bursting: a burst that large would overrun the capture bus ring, and a
/// dropped block is worse than a slow one.
[[nodiscard]] inline SyntheticPace nextSyntheticPace(double dueMs, double nowMs, double blockMs,
                                                     double maxLagMs) noexcept {
    double next = dueMs + blockMs;
    if (nowMs - next > maxLagMs) next = nowMs;  // re-anchor: do not burst
    const double ahead = next - nowMs;
    // Floor, not ceil: waking a hair early only re-checks; the deadline, not the
    // sleep, is what keeps the rate. Under 1 ms ahead: push at once, never spin on wait(0).
    const int waitMs = ahead >= 1.0 ? static_cast<int>(std::floor(ahead)) : 0;
    return {next, waitMs};
}

}  // namespace rta::measure
