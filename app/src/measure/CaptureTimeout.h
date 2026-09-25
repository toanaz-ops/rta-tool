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
[[nodiscard]] constexpr bool captureTimedOut(std::int64_t armedAtMs, std::int64_t nowMs,
                                             std::int64_t timeoutMs) noexcept {
    return (nowMs - armedAtMs) > timeoutMs;
}

}  // namespace rta::measure
