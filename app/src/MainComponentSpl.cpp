// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. MainComponent.cpp's own L6a task W2-E2a split
// (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md "W2-E -- the wiring nobody
// was assigned"; record docs/dsp/2026-09-16-spl-pro-l6a.md §10, §13 Q7): the
// composition root's SPL wiring, moved here to keep MainComponent.cpp under
// the project's 400-line cap -- the same reason MainComponentDelay.cpp and
// MainComponentCalibration.cpp exist. Member-function definitions, declared
// in MainComponent.h, no different in kind from anything else in that class.
//
// WHAT THIS FILE DOES AND DOES NOT DO. It starts/stops the log-writing
// pipeline (AnalysisThread::enableSplLogging/disableSplLogging) when the bus
// this app measures from becomes active or inactive -- a real device opening
// or closing, a sample-rate/device-list change while already active (see
// SplLoggingDecision.h), OR `setSyntheticMode` switching on (the CI-testable
// path: no audio hardware is available on a CI runner). The DECISION itself
// -- off/on/epoch-changed -- is `decideSplLoggingAction`, a pure function in
// measure/SplLoggingDecision.h, proven OFF by
// app/tests/test_spl_logging_decision.cpp on all three CI operating
// systems; `pollSplLogging()` below is only that decision's JUCE-facing
// caller (station-4 fix round, PR #31, verifier finding 2 -- this comment
// used to claim the composition root itself was tested, which it was not:
// app/tests_juce/test_spl_log_wiring.cpp only proves the synthetic-mode
// edge). It does NOT react to a measurement-channel ROLE CHANGE made while
// already active -- SPL-R11 has no preferences store, and re-deriving the
// channel list on every routing change is a defensible follow-up this
// task's own scope (W2-E2a, not W2-E2b) does not reach; the channel list is
// read once, each time logging (re)starts.
#include "MainComponent.h"

#include "measure/SplConfig.h"
#include "measure/SplLoggingDecision.h"
#include "measure/SplSessionFolderName.h"
#include "rta/platform/ChannelConfig.h"

#include <array>
#include <chrono>
#include <span>
#include <string>

void MainComponent::startFreshSplLog(std::uint64_t epoch) {
    // Whichever channels currently hold the Measurement role -- exactly what
    // W2-E2's own task text asks for ("measurement channel(s)"), read once
    // here rather than tracked continuously (this file's own header
    // comment). `channelsWithRole` never allocates and never reads past
    // `buf`'s own size (ChannelConfig.h's own contract).
    std::array<int, static_cast<std::size_t>(rta::platform::kMaxChannels)> buf{};
    const int found =
        audioIo_.bus().config().channelsWithRole(rta::platform::ChannelRole::Measurement, buf);
    const std::span<const int> channels(buf.data(), static_cast<std::size_t>(found));

    // Documents/RTA Tool/spl/<UTC timestamp>-e<epoch>/ -- the JUCE path
    // resolution happens HERE, at the composition root, exactly as the task
    // brief asks; AnalysisThread and SplLogPipeline below it receive a
    // plain path string and know nothing about juce::File
    // (measure_has_no_framework_deps' whole point). `sessionFolderName`
    // (measure/SplSessionFolderName.h, JUCE-free) is what appends the epoch
    // -- round 3, LOW finding 3 -- see that header's own comment for why the
    // 1-second-resolution timestamp alone is not enough.
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const auto sessionDir =
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("RTA Tool")
            .getChildFile("spl")
            .getChildFile(rta::measure::sessionFolderName(now, epoch));
    // Station-4 fix round (PR #31, finding 6): the result USED to be
    // discarded outright. It still is not separately reported here -- a
    // directory `createDirectory()` failed to make means every segment
    // `enableSplLogging` goes on to open underneath it fails too, and THAT
    // failure already reaches the operator through `SplBlockView::
    // logWriteFailed` (SplLogWriter::openFailed()'s own comment: "a
    // directory that does not exist or is not writable makes open() fail").
    // Calling `enableSplLogging` unconditionally, even when this returns
    // false, is what lets that one downstream signal cover both causes
    // rather than needing a second, redundant one here.
    [[maybe_unused]] const bool sessionDirCreated = sessionDir.createDirectory();

    // SplConfig{} defaults, no preferences store (SPL-R11) -- the task
    // brief's own instruction. Wave 3's calibration offset is applied to a
    // LIVE session by starting a new log (W2-E2b, out of scope here); this
    // is the very first log of a session, so the default, uncalibrated
    // offset is correct for it.
    analysisThread_.enableSplLogging(rta::measure::SplConfig{}, channels,
                                     sessionDir.getFullPathName().toStdString());
}

void MainComponent::pollSplLogging() {
    const bool active = isSyntheticMode() || audioIo_.isRunning();
    const std::uint64_t currentEpoch = audioIo_.bus().epoch();

    rta::measure::SplLoggingDecisionInput decisionInput;
    decisionInput.wasActive = splLoggingActive_;
    decisionInput.isActive = active;
    decisionInput.lastEpoch = lastSplEpoch_;
    decisionInput.currentEpoch = currentEpoch;
    const auto action = rta::measure::decideSplLoggingAction(decisionInput);

    if (action == rta::measure::SplLoggingAction::NoOp) {
        return;  // no transition -- enable/disable only ever run on the edge
    }

    const bool wasActive = splLoggingActive_;
    splLoggingActive_ = active;
    lastSplEpoch_ = currentEpoch;

    if (action == rta::measure::SplLoggingAction::Disable) {
        analysisThread_.disableSplLogging();
        return;
    }

    // EnableFresh. `wasActive` distinguishes the two cases the pure function
    // folds together (SplLoggingDecision.h's own comment): an off-to-on edge
    // needs no disable first, but an epoch change while still "active" (a
    // sample-rate or device-list change mid-session, verifier finding 1) is
    // still writing into the OLD session's files and must be closed before
    // a fresh one opens -- never appended to.
    if (wasActive) {
        analysisThread_.disableSplLogging();
    }
    startFreshSplLog(currentEpoch);
}
