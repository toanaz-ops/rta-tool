// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// L6a Wave 3 fix round, verifier round 1 finding 6.
#pragma once

#include <cstdint>

namespace rta::measure {

/// True once `nowMs - armedAtMs` exceeds `timeoutMs`.
///
/// Factored out of MainComponentCalibration.cpp (JUCE-only, untestable with
/// RTA_BUILD_APP=OFF) so the ONE decision that keeps a stuck capture from
/// locking Locate out forever is provable on all three CI operating systems.
/// The capture it guards (`AnalysisThread::armLocateCapture`, shared with
/// Locate) only fills for a wired route -- a calibrator-only rig with no
/// REF channel configured never completes one, and without this the flag
/// that guards BOTH features stays true for the rest of the session.
///
/// `double`, not `std::int64_t` (PR #27 round-2 verifier, LOW; fixed lane
/// L6a task W2-E2a): the caller now feeds `juce::Time::getMillisecondCounterHiRes()`
/// -- a MONOTONIC counter -- rather than `juce::Time::currentTimeMillis()`,
/// the wall clock. A wall clock can jump backwards (NTP step, DST, the
/// operator changing the system clock mid-capture) and a backwards jump here
/// would make `nowMs - armedAtMs` negative, silently defeating this exact
/// timeout for the rest of that capture. `getMillisecondCounterHiRes()`
/// returns a `double` (sub-millisecond resolution since JUCE's own epoch,
/// i.e. process start, not the Unix epoch), so this signature follows it
/// rather than truncating and reintroducing the same rounding this class's
/// own tests already exercise at whole milliseconds.
[[nodiscard]] constexpr bool captureTimedOut(double armedAtMs, double nowMs,
                                             double timeoutMs) noexcept {
    return (nowMs - armedAtMs) > timeoutMs;
}

}  // namespace rta::measure
