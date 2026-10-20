// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. SWEEP lane (docs/plans/2026-09-29-sweep-ir-lane-plan.md)
// task T6: the operator's SWEEP button, in the shape MainComponentSession and
// MainComponentEq already give their halves -- a small class MainComponent
// owns, so MainComponent.h gains a member and an accessor, not a state machine.
//
// One press runs   Idle -> PreRoll -> Playing -> Analysing -> Idle:
//   click      guards, setSource(Sweep), solo output 0, arm the capture;
//   PreRoll    waits >= 100 ms (the arm is picked up at the next 10 ms drain,
//              AnalysisThread.cpp), then armSource() -- so the capture starts
//              BEFORE the sweep does;
//   Playing    waits for the capture, then disarms and hands a COPY of both
//              channels to SweepIrWorker (the 2^21-point transforms take about
//              a second -- far too long for this thread);
//   Analysing  polls the worker, then adds the trace and its IR.
// Everything runs on the message thread except the worker; the audio callback
// does nothing new (project CLAUDE.md "Real-time safety").
//
// The abort paths each disarm the source and say why: the output epoch moved
// (a device switch), the input mode moved (SYNTHETIC on/off -- turning it off
// does NOT bump the epoch, so it needs its own check), or the capture never
// filled (`captureTimedOut`: no REF wired).
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "measure/AnalysisThread.h"
#include "measure/SweepIrWorker.h"
#include "rta/platform/OutputEngine.h"
#include "trace/IrStore.h"
#include "trace/TraceLibrary.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

class MainComponentSweep final {
public:
    enum class State { Idle, PreRoll, Playing, Analysing };
    /// Who pressed: the rail's SWEEP button, ALIGN's SWEEP MAIN / SWEEP SUB, or
    /// the RT60 pane's SWEEP. `Host::swept` carries it, so each pane gets only
    /// the sweeps it asked for (RT60-UI T5: only RT60's result is auto-selected).
    enum class Requester { Rail, Align, Rt60 };

    /// What only the composition root knows. Callbacks rather than a pointer
    /// to MainComponent, so this class never includes it.
    struct Host {
        /// LOCATE, CAL or VERIFY holds the output or the shared capture.
        std::function<bool()> outputBusy;
        std::function<bool()> synthetic;
        std::function<double()> inputSampleRate;
        std::function<std::string()> deviceName;
        std::function<std::string()> channelRoles;
        /// ALIGN-UI T10: outputs the device (or the SYNTHETIC loopback) has,
        /// so a sweep on a named output can refuse one that does not exist.
        std::function<int()> outputChannels;
        /// ALIGN-UI T10: a sweep started with `clickedOnOutput` stored this
        /// trace id, played on this 0-based output, for this requester. Never
        /// fired for the plain SWEEP button.
        std::function<void(const std::string& traceId, int outputChannel, Requester requester)> swept;
    };

    /// Declare AFTER the three references (trap T-1): `worker_` joins in its
    /// destructor, so it must die before they do.
    MainComponentSweep(rta::measure::AnalysisThread& analysis, rta::platform::OutputEngine& output,
                       rta::trace::TraceLibrary& library, Host host);

    void attachTo(juce::Component& parent);
    void layout(juce::Rectangle<int> row);

    /// The button's handler; public so the offscreen harness can call it.
    /// Plays on output 1 and stores `SWEEP @ HH:MM:SS`, as it always has.
    void clicked();
    /// ALIGN-UI T10: the same press on a NAMED 0-based output, stored as
    /// `SWEEP out<N> @ HH:MM:SS` and reported through `Host::swept`. Returns
    /// true when the sweep armed; false (reason in the readout) when refused.
    /// RT60-UI T5: `tailSeconds` overrides the capture tail for THIS press only
    /// (the rail button keeps `request_`'s 3 s), and `requester` rides along to
    /// `Host::swept`.
    bool clickedOnOutput(int outputChannel, Requester requester = Requester::Align,
                         std::optional<double> tailSeconds = std::nullopt);
    /// The readout the operator reads for this sweep (the ALIGN pane repeats it).
    [[nodiscard]] juce::String readoutText() const { return readout_.getText(); }
    /// IRs kept for ALIGN's pickers (`IrStore::find`).
    [[nodiscard]] const rta::trace::IrStore& irStore() const noexcept { return irStore_; }
    /// Called from MainComponent's 2 Hz timer.
    void poll();

    [[nodiscard]] bool busy() const noexcept { return state_ != State::Idle; }

    // --- test hooks, each referenced by spelling from app/tests_juce ---------
    [[nodiscard]] State stateForTest() const noexcept { return state_; }
    [[nodiscard]] juce::String readoutForTest() const { return readout_.getText(); }
    [[nodiscard]] const rta::trace::IrStore& irStoreForTest() const noexcept { return irStore_; }
    [[nodiscard]] const juce::Component& buttonForTest() const noexcept { return button_; }
    [[nodiscard]] const juce::Component& readoutComponentForTest() const noexcept { return readout_; }
    /// The tail the running (or last) press was armed with, for the override case.
    [[nodiscard]] double armedTailSecondsForTest() const noexcept { return armedRequest_.tailSeconds; }
    /// A shorter sweep and tail so a case runs in seconds, not 14 s.
    void setDurationsForTest(double sweepSeconds, double tailSeconds) {
        request_.sweep.durationSec = sweepSeconds;
        request_.tailSeconds = tailSeconds;
    }
    /// Pre-fills the IR store so the eviction wording is reachable in one press.
    void fillIrStoreForTest(int count) {
        for (int i = 0; i < count; ++i) {
            irStore_.add("filler-" + std::to_string(i), std::make_shared<const rta::measure::SweepIr>());
        }
    }
    /// Stores an IR under a library trace's id without a sweep (the RT60 pane's
    /// picker and table need an IR with a known closed form).
    void addIrForTest(const std::string& traceId, std::shared_ptr<const rta::measure::SweepIr> ir) {
        irStore_.add(traceId, std::move(ir));
    }
    void setClockForTest(std::function<double()> nowMs) { nowMs_ = std::move(nowMs); }
    /// Stretches the capture timeout slack (default 5 s) so a saturated box does not
    /// abort a healthy sweep; the settle and the capture window are unchanged.
    void setTimeoutSlackForTest(double ms) { timeoutSlackMs_ = ms; }

private:
    bool startOn(int outputChannel, Requester requester, std::optional<double> tailSeconds);
    void say(const juce::String& text) { readout_.setText(text, juce::dontSendNotification); }
    void abort(const juce::String& why);
    void startAnalysis();
    void finish(const std::shared_ptr<const rta::measure::SweepIrOutcome>& outcome);

    rta::measure::AnalysisThread& analysis_;
    rta::platform::OutputEngine& output_;
    rta::trace::TraceLibrary& library_;
    Host host_;

    juce::TextButton button_{"SWEEP"};
    juce::Label readout_;

    State state_ = State::Idle;
    rta::measure::SweepIrRequest request_;       ///< the shipped config; tests shorten it
    rta::measure::SweepIrRequest armedRequest_;  ///< what the running sweep was armed with
    int armedOutput_ = 0;
    Requester armedRequester_ = Requester::Rail;  ///< not Rail: started by clickedOnOutput
    std::uint64_t armedEpoch_ = 0;
    bool armedSynthetic_ = false;
    std::string armedDeviceName_;
    std::string armedChannelRoles_;
    double armedAtMs_ = 0.0;
    double timeoutMs_ = 0.0;
    std::shared_ptr<const rta::measure::LocateCapture> lastHandled_;
    std::function<double()> nowMs_;
    double timeoutSlackMs_ = 5000.0;  // slack on top of the capture's own duration before it is called stuck
    rta::trace::IrStore irStore_;
    rta::measure::SweepIrWorker worker_;  // LAST: joins first on destruction

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponentSweep)
};
