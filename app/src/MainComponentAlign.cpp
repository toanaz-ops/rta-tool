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
    // Wave B: ALIGN's SWEEP MAIN / SWEEP SUB press this component's own sweep
    // on a named output, and read the IRs it keeps.
    align_.attachSweep(MainComponentAlign::SweepAccess{
        [this](int outputChannel) { return sweep_.clickedOnOutput(outputChannel); },
        [this](const std::string& traceId) { return sweep_.irStore().find(traceId); },
        [this] { return sweep_.readoutText().toStdString(); },
        [this] { return sweep_.busy(); } });
}

void MainComponentAlign::sweepOnSide(rta::measure::Source side) {
    if (!sweep_.start) return;
    const auto& answers = runner_.answers();
    const int channel = side == rta::measure::Source::Main ? answers.mainOutput : answers.subOutput;
    // Recorded before the press and dropped if it is refused: `onSwept` binds
    // only a sweep this button started, never the plain SWEEP button's.
    // A refused press (a sweep is already running, say) leaves the running
    // sweep's pending side alone: its IR still has to bind when it finishes.
    const auto previous = pending_;
    pending_ = PendingSweep{ side, channel };
    if (!sweep_.start(channel)) pending_ = previous;
}

void MainComponentAlign::onSwept(const std::string& traceId, int outputChannel) {
    if (!pending_.has_value() || pending_->outputChannel != outputChannel || !sweep_.find) return;
    runner_.bindIr(pending_->side, traceId, sweep_.find(traceId));
    pending_.reset();
}

rta::view::AlignIrHooks MainComponentAlign::irHooks() {
    rta::view::AlignIrHooks hooks;
    hooks.sweepOnSide = [this](rta::measure::Source side) { sweepOnSide(side); };
    hooks.lookup = [this](const std::string& traceId) {
        return sweep_.find ? sweep_.find(traceId) : std::shared_ptr<const rta::measure::SweepIr>{};
    };
    hooks.choices = [this] {
        std::vector<rta::view::AlignIrChoice> choices;
        if (library_ == nullptr || !sweep_.find) return choices;
        const auto entries = library_->entries();
        // Newest first: the sweep just taken is the one the operator wants.
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            if (it->group == "SWEEP" && sweep_.find(it->traceId) != nullptr) {
                choices.push_back({ it->traceId, it->name });
            }
        }
        return choices;
    };
    hooks.sweepReadout = [this] { return sweep_.readout ? sweep_.readout() : std::string(); };
    hooks.sweepBusy = [this] { return sweep_.busy && sweep_.busy(); };
    return hooks;
}
