// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/AlignRunner.h"

#include "measure/AlignRunnerText.h"
#include "measure/AlignmentWizard.h"
#include "measure/CaptureTimeout.h"
#include "measure/TransferDwell.h"

#include "rta/gen/Noise.h"
#include "rta/gen/Prng.h"

#include <cmath>
#include <cstdio>
#include <utility>

namespace rta::measure {
using namespace detail;
struct AlignRunner::Impl {
    rta::platform::OutputEngine* engine = nullptr;
    rta::trace::TraceLibrary* library = nullptr;
    AlignHost host;
    AlignAnswers answers;
    AlignAnswers runAnswers;  ///< `answers` as of the last press
    std::unique_ptr<AlignmentWizard> wizard;

    std::string status = "ALIGN: not run yet";
    AlignBlock refusal = AlignBlock::None;
    AlignOutcome outcome = AlignOutcome::None;
    WizardRefusal wizardRefusal = WizardRefusal::None;
    std::uint64_t revision = 0;
    std::optional<std::ptrdiff_t> cycleSamples;

    // What the run was started against; every step re-checks them.
    std::uint64_t pressedEpoch = 0;
    bool pressedSynthetic = false;
    std::size_t pressedFftSize = 0;
    double pressedSampleRate = 0.0;
    rta::dsp::TransferAveraging pressedAveraging = rta::dsp::TransferAveraging::Fifo;
    // The clock of THIS step's solo: the dwell is counted from it, never from
    // the arm (each solo re-ramps a different output gate, and the transfer
    // FIFO still holds the previous step's frames).
    std::uint64_t stepStartRendered = 0;
    double stepStartMs = 0.0;

    void say(std::string text) {
        status = std::move(text);
        ++revision;
    }
    [[nodiscard]] bool attached() const {
        return engine != nullptr && library != nullptr && host.environment && host.latest &&
               host.captureConfig && host.freeze && host.nowMs;
    }
    [[nodiscard]] bool measuring() const {
        if (!wizard) return false;
        const auto s = wizard->step();
        return s == WizardStep::MeasuringHigh || s == WizardStep::MeasuringLow || s == WizardStep::MeasuringSum;
    }
    void markStepStart() {
        stepStartRendered = engine->renderedSamples();
        stepStartMs = host.nowMs();
    }
    void abort(AlignOutcome why, std::string text) {
        // disarmSource() first: nothing past a failed run may keep the
        // loudspeaker playing (output-path record sec.8).
        engine->disarmSource();
        wizard.reset();
        outcome = why;
        say(std::move(text));
    }
    [[nodiscard]] Source measuredSource() const {
        const bool high = wizard->step() == WizardStep::MeasuringHigh;
        return (high == (answers.highPass == Source::Main)) ? Source::Main : Source::Sub;
    }
    [[nodiscard]] rta::platform::SourceVariant pinkSource() const {
        const auto seed = static_cast<std::uint64_t>(host.nowMs());
        return rta::gen::PinkNoise(rta::gen::Pcg32(seed, 1), kAlignLevelDbFs);
    }
    void afterSubmit(const AlignAnswers& asked);
    void trySubmit(double now);
};

AlignRunner::AlignRunner() : impl_(std::make_unique<Impl>()) {}
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
std::optional<std::ptrdiff_t> AlignRunner::cycleSamples() const noexcept { return impl_->cycleSamples; }
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
    const Impl& d = *impl_;
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
    Impl& d = *impl_;
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

    // A FRESH wizard per press: arm() refuses unless Answering, and the runner
    // keeps the answers, so a second MEASURE replays them.
    auto fresh = std::make_unique<AlignmentWizard>(*d.engine, d.pinkSource(), config);
    fresh->answerHighPassSide(*a.highPass);
    fresh->answerTopology(*a.topology);
    fresh->answerInversion(*a.inversion);
    fresh->answerCrossoverSeed(a.seedHz);
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
    d.markStepStart();
    const Source first = d.measuredSource();
    d.say(std::string("ALIGN: pink noise on ") + sourceLabel(first) + " (OUT " +
          std::to_string((first == Source::Main ? a.mainOutput : a.subOutput) + 1) +
          ") only, waiting for the excitation to settle");
    return true;
}

bool AlignRunner::pressSum() {
    Impl& d = *impl_;
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
    d.say("SUM: pink noise on MAIN and SUB together, waiting for the excitation to settle");
    return true;
}

bool AlignRunner::answerCycleMs(double milliseconds) {
    Impl& d = *impl_;
    const bool askable = d.wizard && (d.wizard->step() == WizardStep::AskingCycle ||
                                      d.wizard->step() == WizardStep::Computed);
    if (!askable || !std::isfinite(milliseconds)) {
        d.say("OFFSET: run MEASURE first, then type a finite offset in ms");
        return false;
    }
    const auto samples =
        static_cast<std::ptrdiff_t>(std::llround(milliseconds * d.pressedSampleRate / 1000.0));
    d.wizard->answerCycleOffsetSamples(samples);
    d.cycleSamples = samples;
    char text[96];
    std::snprintf(text, sizeof text, "ALIGN: coarse offset %+.2f ms = %+td samples accepted", milliseconds, samples);
    d.say(text);
    return true;
}

void AlignRunner::poll() {
    Impl& d = *impl_;
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

void AlignRunner::Impl::trySubmit(double now) {
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

    const AlignAnswers asked = answers;
    const std::string label = host.timeLabel ? host.timeLabel() : std::string("--");
    const WizardStep before = wizard->step();
    if (before == WizardStep::MeasuringSum) {
        library->add(*trace, "ALIGN SUM @ " + label, kAlignGroup);
        wizard->submitMeasuredSum(std::move(*trace));
        outcome = AlignOutcome::SumMeasured;
        say("SUM: measured sum stored as ALIGN SUM; compare it with the predicted sum");
        return;
    }
    const Source who = measuredSource();
    library->add(*trace, std::string("ALIGN ") + sourceLabel(who) + " @ " + label, kAlignGroup);
    wizard->submitCapture(std::move(*trace));
    if (wizard->lastRefusal() != WizardRefusal::None && wizard->step() == before) {
        wizardRefusal = wizard->lastRefusal();
        abort(AlignOutcome::SettingsChanged,
              std::string("ALIGN aborted: the wizard refused the capture (") +
                  wizardRefusalName(wizard->lastRefusal()) + ")");
        return;
    }
    afterSubmit(asked);
}

void AlignRunner::Impl::afterSubmit(const AlignAnswers& asked) {
    const WizardStep now = wizard->step();
    if (now == WizardStep::MeasuringLow) {
        markStepStart();
        const Source next = measuredSource();
        say(std::string("ALIGN: captured; now pink noise on ") + sourceLabel(next) + " (OUT " +
            std::to_string((next == Source::Main ? asked.mainOutput : asked.subOutput) + 1) + ") only");
        return;
    }
    if (now == WizardStep::MeasuringHigh) return;  // not reachable from a submit; kept explicit
    if (!wizard->verdict().has_value() || now == WizardStep::Answering) {
        wizardRefusal = wizard->lastRefusal();
        abort(AlignOutcome::SettingsChanged,
              std::string("ALIGN aborted: the fit could not run (") + wizardRefusalName(wizard->lastRefusal()) + ")");
        return;
    }
    const Verdict& v = *wizard->verdict();
    if (v.fitRefusal != rta::dsp::CrossoverRefusal::None) {
        outcome = AlignOutcome::FitRefused;
        say(std::string("ALIGN: the fit refused (") + fitRefusalName(v.fitRefusal) + ") -- no suggestion");
        return;
    }
    outcome = AlignOutcome::Measured;
    say(now == WizardStep::AskingCycle
            ? "ALIGN: fit done -- type the coarse offset in ms to choose among the candidates"
            : "ALIGN: fit done");
}

}  // namespace rta::measure
