// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/AlignRunner.h"

#include "measure/AlignRunnerState.h"
#include "measure/AlignRunnerText.h"
#include "measure/CaptureTimeout.h"
#include "measure/TransferDwell.h"

#include <chrono>

#include <cmath>
#include <cstdio>
#include <utility>

namespace rta::measure {
using namespace detail;

AlignRunner::AlignRunner() : impl_(std::make_unique<AlignRunnerState>()) {}
AlignRunner::~AlignRunner() = default;

void AlignRunner::attach(rta::platform::OutputEngine& engine, rta::trace::TraceLibrary& library,
                         AlignHost host) {
    impl_->engine = &engine;
    impl_->library = &library;
    impl_->host = std::move(host);
}

AlignAnswers& AlignRunner::answers() noexcept { return impl_->answers; }
const AlignAnswers& AlignRunner::answers() const noexcept { return impl_->answers; }
const AlignAnswers& AlignRunner::runAnswers() const noexcept { return impl_->runAnswers; }
int AlignRunner::outputChannels() const {
    return impl_->host.environment ? impl_->host.environment().outputChannels : 0;
}
bool AlignRunner::busy() const noexcept { return impl_->measuring(); }
WizardStep AlignRunner::step() const noexcept {
    return impl_->wizard ? impl_->wizard->step() : WizardStep::Answering;
}
const std::string& AlignRunner::status() const noexcept { return impl_->status; }
const std::optional<Verdict>& AlignRunner::verdict() const noexcept {
    static const std::optional<Verdict> kNone;
    return impl_->wizard ? impl_->wizard->verdict() : kNone;
}
std::optional<std::ptrdiff_t> AlignRunner::cycleSamples() const noexcept {
    if (impl_->cycleSamples.has_value()) return impl_->cycleSamples;
    return impl_->wizard ? impl_->wizard->cycleOffsetSamples() : std::nullopt;  // from the IR arrivals
}
bool AlignRunner::cycleFromIr() const noexcept { return impl_->cycleFromIr; }
std::optional<std::ptrdiff_t> AlignRunner::irHintSamples() const noexcept {
    return impl_->wizard ? impl_->wizard->irCycleHintSamples() : std::nullopt;
}
std::optional<SeedDisagreement> AlignRunner::seedDisagreement() const noexcept {
    return impl_->wizard ? impl_->wizard->seedDisagreement() : std::nullopt;
}
bool AlignRunner::engineQuiescent() const noexcept {
    return impl_->engine != nullptr && impl_->engine->sourceIsQuiescent();
}
double AlignRunner::lastComputeMs() const noexcept { return impl_->lastComputeMs; }
double AlignRunner::sampleRate() const noexcept { return impl_->pressedSampleRate; }
const rta::trace::Trace* AlignRunner::mainCapture() const noexcept {
    return impl_->wizard && impl_->wizard->mainCapture() ? &*impl_->wizard->mainCapture() : nullptr;
}
const rta::trace::Trace* AlignRunner::subCapture() const noexcept {
    return impl_->wizard && impl_->wizard->subCapture() ? &*impl_->wizard->subCapture() : nullptr;
}
const rta::trace::Trace* AlignRunner::measuredSum() const noexcept {
    return impl_->wizard && impl_->wizard->measuredSum() ? &*impl_->wizard->measuredSum() : nullptr;
}
std::uint64_t AlignRunner::revision() const noexcept { return impl_->revision; }
AlignBlock AlignRunner::lastRefusalForTest() const noexcept { return impl_->refusal; }
AlignOutcome AlignRunner::outcomeForTest() const noexcept { return impl_->outcome; }
WizardRefusal AlignRunner::wizardRefusalForTest() const noexcept { return impl_->wizardRefusal; }

AlignBlocker AlignRunner::blocker() const {
    const AlignRunnerState& d = *impl_;
    const auto block = [](AlignBlock kind, const std::string& extra = {}) {
        std::string text = alignBlockText(kind);
        if (!extra.empty()) text += " (" + extra + ")";
        return AlignBlocker{ kind, std::move(text) };
    };
    if (!d.attached()) return block(AlignBlock::NotAttached);
    if (d.measuring()) return block(AlignBlock::Running);
    const AlignAnswers& a = d.answers;
    if (!a.complete()) {
        std::string missing;
        const auto add = [&missing](bool unanswered, const char* what) {
            if (!unanswered) return;
            missing += missing.empty() ? what : std::string(", ") + what;
        };
        add(!a.highPass.has_value(), "high-pass side");
        add(!a.topology.has_value(), "topology");
        add(!a.inversion.has_value(), "inversion");
        add(!a.seedAnswered, "crossover seed");
        return block(AlignBlock::MissingAnswer, "still to answer: " + missing);
    }
    if (a.mainOutput == a.subOutput) return block(AlignBlock::SameOutput);
    const AlignEnvironment env = d.host.environment();
    if (!env.synthetic && !env.deviceRunning) return block(AlignBlock::DeviceNotRunning);
    if (env.captureBusy) return block(AlignBlock::CaptureBusy);
    if (a.mainOutput < 0 || a.subOutput < 0 || a.mainOutput >= env.outputChannels ||
        a.subOutput >= env.outputChannels) {
        return block(AlignBlock::OutputMissing, "this device has " + std::to_string(env.outputChannels) +
                                                    " output(s)");
    }
    const SnapshotPtr snapshot = d.host.latest();
    if (!snapshot || !snapshot->transfer.has_value()) return block(AlignBlock::NoTransfer);
    if (snapshot->sampleRate != d.engine->sampleRate()) {
        char detail[96];
        std::snprintf(detail, sizeof detail, "input %.0f Hz, output %.0f Hz", snapshot->sampleRate,
                      d.engine->sampleRate());
        return block(AlignBlock::SampleRateMismatch, detail);
    }
    if (d.host.captureConfig().transferAveraging == rta::dsp::TransferAveraging::Exponential) {
        return block(AlignBlock::ExponentialAveraging);
    }
    if (!d.engine->sourceIsQuiescent()) return block(AlignBlock::EngineNotQuiescent);
    return block(AlignBlock::None);
}

bool AlignRunner::press() {
    AlignRunnerState& d = *impl_;
    const AlignBlocker why = blocker();
    if (why.kind != AlignBlock::None) {
        d.refusal = why.kind;
        d.say("MEASURE refused: " + why.text);
        return false;
    }
    const AlignAnswers& a = d.answers;
    AlignmentWizard::Config config;
    config.mainOutputChannel = a.mainOutput;
    config.subOutputChannel = a.subOutput;
    config.sampleRate = d.engine->sampleRate();
    config.matchedPairAgreement = kAlignMatchedPairAgreement;

    d.noteRunLevel();
    // A FRESH wizard per press: arm() refuses unless Answering, and the runner
    // keeps the answers, so a second MEASURE replays them.
    auto fresh = std::make_unique<AlignmentWizard>(*d.engine, d.pinkSource(), config);
    fresh->answerHighPassSide(*a.highPass);
    fresh->answerTopology(*a.topology);
    fresh->answerInversion(*a.inversion);
    fresh->answerCrossoverSeed(a.seedHz);
    // The IRs the operator bound, keyed by SOURCE: which is the high-pass side
    // is the wizard's business (question (a)), so no role is chosen here.
    const std::string irNote = d.supplyBoundIrs(*fresh);
    fresh->arm();
    if (fresh->lastRefusal() != WizardRefusal::None) {
        d.wizardRefusal = fresh->lastRefusal();
        d.refusal = AlignBlock::EngineNotQuiescent;
        d.say("MEASURE refused: " + alignBlockText(AlignBlock::EngineNotQuiescent));
        return false;
    }
    d.wizard = std::move(fresh);
    d.runAnswers = a;

    const SnapshotPtr snapshot = d.host.latest();
    d.pressedFftSize = snapshot->fftSize;
    d.pressedSampleRate = snapshot->sampleRate;
    d.pressedAveraging = d.host.captureConfig().transferAveraging;
    d.pressedEpoch = d.engine->outputEpoch();
    d.pressedSynthetic = d.host.environment().synthetic;
    d.refusal = AlignBlock::None;
    d.outcome = AlignOutcome::None;
    d.cycleSamples.reset();
    d.cycleFromIr = false;
    d.markStepStart();
    const Source first = d.measuredSource();
    d.say(std::string("ALIGN: pink noise on ") + sourceLabel(first) + " (OUT " +
          std::to_string((first == Source::Main ? a.mainOutput : a.subOutput) + 1) +
          ") only, waiting for the excitation to settle" + d.runNote() + irNote);
    return true;
}

bool AlignRunner::pressSum() {
    AlignRunnerState& d = *impl_;
    if (!d.attached() || !d.wizard || !d.wizard->verdict().has_value()) {
        d.say("SUM: run MEASURE first");
        return false;
    }
    const WizardStep step = d.wizard->step();
    if (step == WizardStep::AskingCycle) {
        d.say("SUM: answer the coarse offset first");
        return false;
    }
    if (step != WizardStep::Computed && step != WizardStep::AskingPolarity) {
        d.say(step == WizardStep::Done ? "SUM: already measured -- press MEASURE for a new run"
                                       : "SUM: wait for the current step to finish");
        return false;
    }
    const AlignEnvironment env = d.host.environment();
    if (!env.synthetic && !env.deviceRunning) {
        d.say("SUM refused: " + alignBlockText(AlignBlock::DeviceNotRunning));
        return false;
    }
    if (env.captureBusy) {
        d.say("SUM refused: " + alignBlockText(AlignBlock::CaptureBusy));
        return false;
    }
    // The slot may have been taken by another feature since MEASURE: arming
    // whatever it holds would measure the wrong signal without saying so.
    d.noteRunLevel();
    if (!d.engine->setSource(d.pinkSource())) {
        d.say("SUM refused: " + alignBlockText(AlignBlock::EngineNotQuiescent));
        return false;
    }
    d.wizard->beginMeasuredSum();
    if (d.wizard->lastRefusal() != WizardRefusal::None) {
        d.wizardRefusal = d.wizard->lastRefusal();
        d.say(std::string("SUM refused: ") + wizardRefusalName(d.wizard->lastRefusal()));
        return false;
    }
    d.pressedEpoch = d.engine->outputEpoch();
    d.pressedSynthetic = env.synthetic;
    d.outcome = AlignOutcome::None;
    d.markStepStart();
    d.sayRun("SUM: pink noise on MAIN and SUB together, waiting for the excitation to settle");
    return true;
}

bool AlignRunner::answerCycleMs(double milliseconds) {
    AlignRunnerState& d = *impl_;
    const bool askable = d.wizard && (d.wizard->step() == WizardStep::AskingCycle ||
                                      d.wizard->step() == WizardStep::Computed);
    if (!askable || !std::isfinite(milliseconds)) {
        d.say("OFFSET: run MEASURE first, then type a finite offset in ms");
        return false;
    }
    const bool overridesIr = d.cycleFromIr;  // a typed offset always wins over the IR integer
    const auto samples =
        static_cast<std::ptrdiff_t>(std::llround(milliseconds * d.pressedSampleRate / 1000.0));
    d.wizard->answerCycleOffsetSamples(samples);
    d.cycleFromIr = false;
    d.cycleSamples = samples;
    char text[160];
    std::snprintf(text, sizeof text, "ALIGN: coarse offset %+.2f ms = %+td samples accepted%s", milliseconds, samples,
                  overridesIr ? " (your typed offset, not the bound IRs')" : "");
    d.say(text);
    return true;
}

void AlignRunner::poll() {
    AlignRunnerState& d = *impl_;
    if (!d.measuring()) return;
    const AlignEnvironment env = d.host.environment();
    if (env.synthetic != d.pressedSynthetic) {
        d.abort(AlignOutcome::OutputChanged, "ALIGN aborted: the input mode changed mid-run (SYNTHETIC on/off)");
        return;
    }
    if (!env.synthetic && !env.deviceRunning) {
        d.abort(AlignOutcome::DeviceStopped, "ALIGN aborted: the audio device stopped mid-run");
        return;
    }
    if (d.engine->outputEpoch() != d.pressedEpoch) {
        d.abort(AlignOutcome::OutputChanged, "ALIGN aborted: the output device was reopened mid-run");
        return;
    }
    d.trySubmit(d.host.nowMs());
}

void AlignRunnerState::trySubmit(double now) {
    wizard->poll();
    const CaptureConfig config = host.captureConfig();
    const double fs = engine->sampleRate();
    const double dwellSeconds = transferDwellSeconds(config, pressedFftSize, pressedSampleRate);
    const double dwellMs = dwellSeconds * 1000.0;
    const auto stalled = [&] { return captureTimedOut(stepStartMs, now, dwellMs + kAlignTimeoutMs); };
    const auto stall = [&] {
        char text[128];
        std::snprintf(text, sizeof text,
                      "ALIGN timed out: no usable post-excitation snapshot within %.0f ms of the dwell",
                      kAlignTimeoutMs);
        abort(AlignOutcome::TimedOut, text);
    };
    // Counted from THIS step's solo, on the clock the excitation runs on. A
    // snapshot published before the FIFO has turned over still holds the
    // PREVIOUS output's frames; submitting it would store one output's
    // response under the other's name.
    const std::uint64_t rendered = engine->renderedSamples();
    const double renderedSince =
        rendered >= stepStartRendered ? static_cast<double>(rendered - stepStartRendered) : 0.0;
    if (!wizard->captureWindowOpen() || renderedSince < dwellSeconds * fs) {
        if (stalled()) stall();
        return;
    }
    const SnapshotPtr snapshot = host.latest();
    if (!snapshot || !snapshot->transfer.has_value() || !snapshot->transfer->coherence.has_value()) {
        if (stalled()) stall();  // the coherence gate has not opened yet: keep waiting
        return;
    }
    if (snapshot->fftSize != pressedFftSize || snapshot->sampleRate != pressedSampleRate ||
        config.transferAveraging != pressedAveraging) {
        abort(AlignOutcome::SettingsChanged,
              "ALIGN aborted: the transfer fftSize, sample rate or averaging mode changed mid-run");
        return;
    }
    auto trace = host.freeze(*snapshot);
    if (!trace.has_value()) {
        if (stalled()) stall();
        return;
    }

    const AlignAnswers asked = runAnswers;  // the run's own answers, never the combos' live ones
    const std::string label = host.timeLabel ? host.timeLabel() : std::string("--");
    const WizardStep before = wizard->step();
    if (before == WizardStep::MeasuringSum) {
        library->add(*trace, "ALIGN SUM @ " + label + traceTag(), kAlignGroup);
        wizard->submitMeasuredSum(std::move(*trace));
        outcome = AlignOutcome::SumMeasured;
        sayRun("SUM: measured sum stored as ALIGN SUM; compare it with the predicted sum");
        return;
    }
    const Source who = measuredSource();
    library->add(*trace, std::string("ALIGN ") + sourceLabel(who) + " @ " + label + traceTag(), kAlignGroup);
    // The LOW step's submit runs the fit and the polarity table on this thread.
    // Timed so the PR can say whether it needs a worker (EQ risk-2 precedent).
    const auto computeStart = std::chrono::steady_clock::now();
    wizard->submitCapture(std::move(*trace));
    if (before == WizardStep::MeasuringLow) {
        lastComputeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - computeStart).count();
    }
    if (wizard->lastRefusal() != WizardRefusal::None && wizard->step() == before) {
        wizardRefusal = wizard->lastRefusal();
        abort(AlignOutcome::SettingsChanged,
              std::string("ALIGN aborted: the wizard refused the capture (") +
                  wizardRefusalName(wizard->lastRefusal()) + ")");
        return;
    }
    afterSubmit(asked);
}

void AlignRunnerState::afterSubmit(const AlignAnswers& asked) {
    const WizardStep now = wizard->step();
    if (now == WizardStep::MeasuringLow) {
        markStepStart();
        const Source next = measuredSource();
        sayRun(std::string("ALIGN: captured; now pink noise on ") + sourceLabel(next) + " (OUT " +
               std::to_string((next == Source::Main ? asked.mainOutput : asked.subOutput) + 1) + ") only");
        return;
    }
    if (!wizard->verdict().has_value() || now == WizardStep::Answering) {
        wizardRefusal = wizard->lastRefusal();
        abort(AlignOutcome::SettingsChanged,
              std::string("ALIGN aborted: the fit could not run (") + wizardRefusalName(wizard->lastRefusal()) + ")");
        return;
    }
    const Verdict& v = *wizard->verdict();
    if (v.fitRefusal != rta::dsp::CrossoverRefusal::None) {
        outcome = AlignOutcome::FitRefused;
        sayRun(std::string("ALIGN: the fit refused (") + fitRefusalName(v.fitRefusal) + ") -- no suggestion");
        return;
    }
    outcome = AlignOutcome::Measured;
    if (now == WizardStep::AskingCycle) {
        if (wizard->seedDisagreement().has_value()) {
            const SeedDisagreement& s = *wizard->seedDisagreement();
            char text[200];
            std::snprintf(text, sizeof text,
                          "ALIGN: fit done -- seed and measured crossing disagree (%.0f Hz vs %.0f Hz), so the "
                          "IRs did not choose; type the coarse offset in ms (both IR hints are shown)",
                          s.seedHz, s.crossingHz);
            sayRun(text);
            return;
        }
        sayRun(wizard->irCycleHintSamples().has_value()
                   ? "ALIGN: fit done -- the bound IRs fit no candidate clearly (see the hint); type the coarse offset in ms"
                   : "ALIGN: fit done -- type the coarse offset in ms to choose among the candidates");
        return;
    }
    cycleFromIr = wizard->cycleOffsetFromIr();
    sayRun(cycleFromIr ? "ALIGN: fit done -- the bound IRs chose the cycle (type an offset to override)"
                       : "ALIGN: fit done");
}

}  // namespace rta::measure
