// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. L7-EQ UI wave B, task T7
// (docs/plans/2026-09-29-eq-ui-lane-plan.md): the composition root's half of
// VERIFY. The state machine, the dwell, the timeout and ADOPT all live in
// rta::measure::EqVerifyRunner (JUCE-free, proven with no device); this file
// only tells it what the real app knows -- whether a device is running, what
// the analysis thread last published, how to freeze a snapshot into a Trace,
// and whether LOCATE or CAL holds the output or the shared capture.
//
// The reverse direction -- LOCATE and CAL refusing while a VERIFY runs -- is
// a guard at the top of each of their handlers (MainComponentDelay.cpp,
// MainComponentCalibration.cpp), reading `eq_.verifyBusy()`. Both directions
// are needed: LOCATE ignores `setSource`'s return value and disarms at the
// end of its capture (plan risk 3), so an overlap would silently kill the
// excitation VERIFY is measuring.
#include "MainComponent.h"

#include "measure/EqVerifyRunner.h"

void MainComponent::wireEqVerify() {
    rta::measure::EqVerifyHost host;
    host.environment = [this] {
        rta::measure::EqVerifyEnvironment env;
        env.synthetic = isSyntheticMode();
        env.deviceRunning = audioIo_.isRunning();
        env.captureBusy = locateWaitingForSettle_ || locateCaptureArmed_ || calibrationCaptureArmed_;
        return env;
    };
    host.latest = [this] { return analysisThread_.latest(); };
    host.captureConfig = [this] { return analysisThread_.captureConfig(); };
    host.freeze = [this](const rta::measure::Snapshot& snapshot) {
        return freezeSnapshot(snapshot, rta::view::PaneView::Transfer);
    };
    host.nowMs = [] { return juce::Time::getMillisecondCounterHiRes(); };
    host.timeLabel = [] { return juce::Time::getCurrentTime().formatted("%H:%M:%S").toStdString(); };
    eq_.attachVerify(audioIo_.output(), library_, std::move(host));
}
