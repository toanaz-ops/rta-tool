// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// Station-4 fix round, PR #31, verifier findings 1 (HIGH) and 2 (MEDIUM).
//
// MainComponentSpl.cpp's pollSplLogging() used to decide everything inline,
// against `audioIo_`/`analysisThread_` directly -- untestable OFF, and the
// file's own header comment claimed it WAS tested by
// app/tests_juce/test_spl_log_wiring.cpp, which only proves the synthetic-
// mode edge, never the composition root's own decision. That let a real
// defect through: `AudioIo::setSampleRate` (platform/src/AudioIo.cpp) and a
// device-list reconfiguration (AudioIo_Devices.cpp) both restart the device
// WITHOUT clearing `running_`, so `isRunning()` never edges false-then-true
// across a sample-rate or device change made mid-session -- the log kept
// writing into the OLD session's files at the NEW rate, unlabelled.
//
// `CaptureBus::epoch()` (platform/include/rta/platform/CaptureBus.h) already
// exists for exactly this: `AnalysisThread::rebuildAnalysersIfEpochChanged`
// (AnalysisThread.cpp) already rebuilds every Analyser on an epoch change.
// This function is that same rule applied to the log: a device staying
// "active" across an epoch bump is not the same session, and gets a FRESH
// UTC folder, never an appended one.
#pragma once

#include <cstdint>

namespace rta::measure {

/// What `pollSplLogging()` should do this tick. `EnableFresh` covers BOTH
/// the off-to-on edge and an epoch change while already on -- the caller
/// tells the two apart by whether it was already active (see
/// `SplLoggingDecisionInput::wasActive`), and calls `disable()` first only
/// in the second case, before building the new folder and calling
/// `enable()`.
enum class SplLoggingAction { NoOp, Disable, EnableFresh };

/// Pure decision input. No juce::AudioIODevice, no AnalysisThread pointer --
/// the four facts the decision actually depends on, read at the composition
/// root and handed in, so this function is provable in the RTA_BUILD_APP=OFF
/// target on all three CI operating systems.
struct SplLoggingDecisionInput {
    bool wasActive = false;
    bool isActive = false;
    /// `audioIo_.bus().epoch()` as of the PREVIOUS tick this function
    /// returned something other than NoOp. Meaningless when `!wasActive`.
    std::uint64_t lastEpoch = 0;
    /// `audioIo_.bus().epoch()` as of THIS tick.
    std::uint64_t currentEpoch = 0;
};

[[nodiscard]] inline SplLoggingAction decideSplLoggingAction(
    const SplLoggingDecisionInput& input) noexcept {
    if (!input.wasActive && !input.isActive) {
        return SplLoggingAction::NoOp;  // stayed off
    }
    if (input.wasActive && !input.isActive) {
        return SplLoggingAction::Disable;  // active -> inactive edge
    }
    if (!input.wasActive && input.isActive) {
        return SplLoggingAction::EnableFresh;  // inactive -> active edge
    }
    // Both true: the only remaining question is whether the bus underneath
    // an unbroken "active" reading is still the SAME session. A sample-rate
    // or device-list change bumps the epoch without ever touching
    // `isRunning()` (this header's own comment above), so the epoch is the
    // one signal that can catch it.
    return input.lastEpoch == input.currentEpoch ? SplLoggingAction::NoOp
                                                  : SplLoggingAction::EnableFresh;
}

}  // namespace rta::measure
