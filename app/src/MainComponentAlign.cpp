// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. ALIGN-UI lane, task T6
// (docs/plans/2026-10-20-align-ui-lane-plan.md): the composition root's half of
// ALIGN. The state machine, the dwell, the timeout and the library entries all
// live in rta::measure::AlignRunner (JUCE-free, proven with no device); this
// file only tells it what the real app knows -- whether a device (or the
// SYNTHETIC loopback) is running, what the analysis thread last published, how
// many outputs there are, how to freeze a snapshot into a Trace, and whether
// LOCATE, CAL, SWEEP or VERIFY holds the output or the shared capture.
#include "MainComponent.h"

void MainComponent::wireAlign() {
    rta::measure::AlignHost host;
    host.environment = [this] {
        rta::measure::AlignEnvironment env;
        env.synthetic = isSyntheticMode();
        env.deviceRunning = audioIo_.isRunning();
        env.captureBusy = outputBusy();
        env.outputChannels = isSyntheticMode() ? rta::measure::SyntheticInput::kOutputChannels
                                               : audioIo_.currentState().numOutputChannels;
        return env;
    };
    host.latest = [this] { return analysisThread_.latest(); };
    host.captureConfig = [this] { return analysisThread_.captureConfig(); };
    // STORE's own conversion, always the TRANSFER block: the wizard fits a
    // coherent transfer function, never a magnitude-only RTA curve.
    host.freeze = [this](const rta::measure::Snapshot& snapshot) {
        return freezeSnapshot(snapshot, rta::view::PaneView::Transfer);
    };
    host.nowMs = [] { return juce::Time::getMillisecondCounterHiRes(); };
    host.timeLabel = [] { return juce::Time::getCurrentTime().formatted("%H:%M:%S").toStdString(); };
    align_.attach(audioIo_.output(), library_, std::move(host));
}
