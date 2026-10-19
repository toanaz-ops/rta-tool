// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See MainComponentSweep.h for the state machine.
#include "MainComponentSweep.h"

#include <az_ui/az_ui.h>

#include "measure/CaptureTimeout.h"
#include "measure/OutputPolicy.h"
#include "rta/gen/Sweep.h"
#include "trace/CaptureConverter.h"

#include <cmath>
#include <utility>

namespace {

// Owner decision 3: the capture holds 1.0 s of pre-roll, the sweep, and the
// tail. One 2 Hz tick (0.5 s) plus the 100 ms settle fit inside the 1.0 s, and
// the pipeline's own tail check (TailTooShort) catches a stalled tick.
constexpr double kPreRollSeconds = 1.0;
// The arm is picked up at the next drain and drains run every 10 ms; 100 ms
// puts the capture's first sample ahead of the sweep's.
constexpr double kSettleMs = 100.0;
// Slack on top of the capture's own duration before it is called stuck.
constexpr double kTimeoutSlackMs = 5000.0;

juce::String signedWhole(long value) { return (value >= 0 ? "+" : "") + juce::String(value); }
juce::String signedOneDecimal(double value) {
    return (value >= 0.0 ? "+" : "") + juce::String(value, 1);
}

}  // namespace

MainComponentSweep::MainComponentSweep(rta::measure::AnalysisThread& analysis,
                                       rta::platform::OutputEngine& output,
                                       rta::trace::TraceLibrary& library, Host host)
    : analysis_(analysis),
      output_(output),
      library_(library),
      host_(std::move(host)),
      nowMs_([] { return juce::Time::getMillisecondCounterHiRes(); }) {
    button_.getProperties().set(az::ui::hintProperty,
                                "log sweep 20 Hz-20 kHz, out 1, needs REF on route 1");
    button_.onClick = [this] { clicked(); };
    readout_.setText("sweep: not run yet", juce::dontSendNotification);
    readout_.setJustificationType(juce::Justification::centredLeft);
}

void MainComponentSweep::attachTo(juce::Component& parent) {
    parent.addAndMakeVisible(button_);
    parent.addAndMakeVisible(readout_);
}

void MainComponentSweep::layout(juce::Rectangle<int> row) {
    const int buttonWidth = (row.getWidth() - az::ui::gap) / 4;
    button_.setBounds(row.removeFromLeft(buttonWidth));
    row.removeFromLeft(az::ui::gap);
    readout_.setBounds(row);
}

void MainComponentSweep::clicked() {
    if (state_ != State::Idle) {
        say("sweep: already running");
        return;
    }
    if (host_.outputBusy()) {
        say("sweep: refused -- LOCATE, CAL or VERIFY is using the output");
        return;
    }
    // setSource fails unless the engine is quiescent (OutputEngine.h): refuse
    // here, with a reason, rather than arm a capture nothing will excite.
    if (!output_.sourceIsQuiescent()) {
        say("sweep: refused -- the output is still playing a source");
        return;
    }
    const double fs = output_.sampleRate();
    if (!(fs > 0.0) || host_.inputSampleRate() != fs) {
        say("sweep: refused -- input and output sample rates differ");
        return;
    }

    armedRequest_ = request_;
    armedRequest_.sweep.sampleRate = fs;
    rta::gen::Sweep sweep(armedRequest_.sweep);
    const auto captureSamples = static_cast<std::size_t>(
        std::ceil((kPreRollSeconds + sweep.durationSec() + armedRequest_.tailSeconds) * fs));
    if (!output_.setSource(std::move(sweep))) {
        say("sweep: refused -- the output is still playing a source");
        return;
    }
    rta::measure::soloOutput(output_, /*outputChannel=*/0);  // route 0 only (decision 10)
    analysis_.armLocateCapture(/*routeIndex=*/0, captureSamples);

    armedEpoch_ = output_.outputEpoch();
    armedSynthetic_ = host_.synthetic();
    armedAtMs_ = nowMs_();
    // Click-time metadata, as STORE reads it at click time: the capture belongs
    // to THIS device and role assignment even if SYNTHETIC or the device
    // changes while the worker runs (ALIGN compares the role string).
    armedDeviceName_ = host_.deviceName();
    armedChannelRoles_ = host_.channelRoles();
    timeoutMs_ = 1000.0 * static_cast<double>(captureSamples) / fs + kTimeoutSlackMs;
    state_ = State::PreRoll;
    say("sweep: arming...");
}

void MainComponentSweep::abort(const juce::String& why) {
    output_.disarmSource();
    state_ = State::Idle;
    say("sweep: aborted -- " + why);
}

void MainComponentSweep::poll() {
    if (state_ == State::Idle) return;

    if (state_ == State::Analysing) {
        if (const auto outcome = worker_.latest()) finish(outcome);
        return;
    }

    // PreRoll and Playing share the abort paths. The source is disarmed by
    // abort(); the capture accumulator cannot be cancelled and is cleared by
    // the next arm.
    if (output_.outputEpoch() != armedEpoch_ || host_.synthetic() != armedSynthetic_) {
        abort("the device or input mode changed");
        return;
    }

    if (state_ == State::PreRoll) {
        if (rta::measure::captureTimedOut(armedAtMs_, nowMs_(), timeoutMs_)) {
            abort("capture never started -- no REF on route 1?");
            return;
        }
        if (nowMs_() - armedAtMs_ < kSettleMs) return;
        output_.armSource();
        state_ = State::Playing;
        say("sweep: playing...");
        return;
    }

    // Playing: only this poll consumes a capture, tracked by its own pointer.
    const auto capture = analysis_.locateCapture();
    if (capture != nullptr && capture != lastHandled_) {
        lastHandled_ = capture;
        startAnalysis();
        return;
    }
    if (rta::measure::captureTimedOut(armedAtMs_, nowMs_(), timeoutMs_)) {
        abort("no capture -- no REF on route 1?");
    }
}

void MainComponentSweep::startAnalysis() {
    // disarmSource() before the copy: nothing past this point may keep the
    // excitation playing.
    output_.disarmSource();
    worker_.start(rta::measure::SweepCapture{lastHandled_->reference, lastHandled_->measurement},
                  armedRequest_);
    lastHandled_.reset();  // the worker owns its copy; do not hold the capture's ~5 MB
    state_ = State::Analysing;
    say("sweep: analysing...");
}

void MainComponentSweep::finish(const std::shared_ptr<const rta::measure::SweepIrOutcome>& owner) {
    const auto& outcome = *owner;
    state_ = State::Idle;
    if (outcome.status != rta::measure::SweepIrStatus::Ok) {
        say("sweep: refused -- " + juce::String(outcome.message));
        return;
    }
    auto trace = rta::trace::traceFromSweepIr(outcome, armedDeviceName_, armedChannelRoles_);
    if (!trace.has_value()) {
        say("sweep: no trace could be built from this capture");
        return;
    }
    const juce::String name = "SWEEP @ " + juce::Time::getCurrentTime().formatted("%H:%M:%S");
    const auto id = library_.add(std::move(*trace), name.toStdString(), "SWEEP");
    if (id.empty()) {
        say("sweep: failed (duplicate capture id)");
        return;
    }
    // Aliases the outcome: the IR stays alive with it, with no 4.6 MB copy.
    const bool evicts = irStore_.size() == rta::trace::IrStore::kCapacity;
    irStore_.add(id, std::shared_ptr<const rta::measure::SweepIr>(owner, &owner->ir));

    const double ms = 1000.0 * static_cast<double>(outcome.delaySamples) / outcome.ir.sampleRate;
    juce::String text = "IR " + signedWhole(outcome.delaySamples) + " samples (" + signedOneDecimal(ms) +
                        " ms) " + juce::String(juce::CharPointer_UTF8("\xc2\xb7")) + " loop " +
                        signedOneDecimal(outcome.loopMinDb) + ".." + signedOneDecimal(outcome.loopMaxDb) +
                        " dB";
    // Decision 6: IRs live in memory only, capped; say so where the operator
    // looks, and say when this one pushed the oldest out.
    text += " " + juce::String(juce::CharPointer_UTF8("Â·")) + " IRs " + juce::String(irStore_.size()) + "/" +
            juce::String(rta::trace::IrStore::kCapacity) + (evicts ? " (oldest dropped)" : "");
    if (outcome.measurementClipped) text += " -- CLIPPED, harmonics at negative time";
    say(text);
}
