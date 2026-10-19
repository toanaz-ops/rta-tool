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
#include <string>

class MainComponentSweep final {
public:
    enum class State { Idle, PreRoll, Playing, Analysing };

    /// What only the composition root knows. Callbacks rather than a pointer
    /// to MainComponent, so this class never includes it.
    struct Host {
        /// LOCATE, CAL or VERIFY holds the output or the shared capture.
        std::function<bool()> outputBusy;
        std::function<bool()> synthetic;
        std::function<double()> inputSampleRate;
        std::function<std::string()> deviceName;
        std::function<std::string()> channelRoles;
    };

    /// Declare AFTER the three references (trap T-1): `worker_` joins in its
    /// destructor, so it must die before they do.
    MainComponentSweep(rta::measure::AnalysisThread& analysis, rta::platform::OutputEngine& output,
                       rta::trace::TraceLibrary& library, Host host);

    void attachTo(juce::Component& parent);
    void layout(juce::Rectangle<int> row);

    /// The button's handler; public so the offscreen harness can call it.
    void clicked();
    /// Called from MainComponent's 2 Hz timer.
    void poll();

    [[nodiscard]] bool busy() const noexcept { return state_ != State::Idle; }

    // --- test hooks, each referenced by spelling from app/tests_juce ---------
    [[nodiscard]] State stateForTest() const noexcept { return state_; }
    [[nodiscard]] juce::String readoutForTest() const { return readout_.getText(); }
    [[nodiscard]] const rta::trace::IrStore& irStoreForTest() const noexcept { return irStore_; }
    [[nodiscard]] const juce::Component& buttonForTest() const noexcept { return button_; }
    [[nodiscard]] const juce::Component& readoutComponentForTest() const noexcept { return readout_; }
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
    void setClockForTest(std::function<double()> nowMs) { nowMs_ = std::move(nowMs); }

private:
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
    std::uint64_t armedEpoch_ = 0;
    bool armedSynthetic_ = false;
    std::string armedDeviceName_;
    std::string armedChannelRoles_;
    double armedAtMs_ = 0.0;
    double timeoutMs_ = 0.0;
    std::shared_ptr<const rta::measure::LocateCapture> lastHandled_;
    std::function<double()> nowMs_;
    rta::trace::IrStore irStore_;
    rta::measure::SweepIrWorker worker_;  // LAST: joins first on destruction

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponentSweep)
};
