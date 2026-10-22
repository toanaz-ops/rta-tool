// SPDX-License-Identifier: AGPL-3.0-or-later
// Internal to AlignRunner.cpp / AlignRunnerIr.cpp: the runner's state. Split out
// of AlignRunner.cpp when wave B's IR binding would have taken it past the
// 400-line cap; the seam is "what the run holds" (here) versus "what the
// operator may do with it" (AlignRunner.cpp). Never included by a
// message-thread header: it pulls in the wizard, and so rta/ir/*.
#pragma once

#include "measure/AlignRunner.h"
#include "measure/AlignmentWizard.h"
#include "measure/SweepIrOutcome.h"

#include "rta/gen/Noise.h"
#include "rta/gen/Prng.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace rta::measure {

struct AlignRunnerState {
    rta::platform::OutputEngine* engine = nullptr;
    rta::trace::TraceLibrary* library = nullptr;
    AlignHost host;
    AlignAnswers answers;
    AlignAnswers runAnswers;  ///< `answers` as of the last press
    std::unique_ptr<AlignmentWizard> wizard;

    /// H7 T13: what the last press played, "-30.0 dBFS RMS" ("" with no drive).
    /// Every status after the arm and every stored trace name carries it.
    std::string runLevelText;

    std::string status = "ALIGN: not run yet";
    AlignBlock refusal = AlignBlock::None;
    AlignOutcome outcome = AlignOutcome::None;
    WizardRefusal wizardRefusal = WizardRefusal::None;
    std::uint64_t revision = 0;
    std::optional<std::ptrdiff_t> cycleSamples;  ///< the operator's typed offset, in samples
    /// True when the run's integer came from IR arrivals, not from the operator.
    bool cycleFromIr = false;
    /// Wall time of the wizard's compute() on the message thread, last run.
    double lastComputeMs = 0.0;

    /// Wave B: the IR the operator bound to each SOURCE (nullptr: none), and
    /// the trace id it came from. Supplied to a fresh wizard at every press.
    std::shared_ptr<const SweepIr> irMain, irSub;
    std::string irMainId, irSubId;

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
        return (high == (runAnswers.highPass == Source::Main)) ? Source::Main : Source::Sub;
    }
    [[nodiscard]] double levelDb() const { return host.drive ? host.drive().levelDb : kAlignLevelDbFs; }
    void noteRunLevel() {
        runLevelText = host.drive ? measurementLevelText(levelDb(), LevelConvention::Rms) : std::string();
    }
    [[nodiscard]] std::string runNote() const { return runLevelText.empty() ? std::string() : " @ " + runLevelText; }
    void sayRun(std::string text) { say(std::move(text) + runNote()); }
    [[nodiscard]] rta::platform::SourceVariant pinkSource() const {
        const auto seed = static_cast<std::uint64_t>(host.nowMs());
        return rta::gen::PinkNoise(rta::gen::Pcg32(seed, 1), levelDb());
    }
    /// " (-30.0 dBFS RMS)" appended to a stored trace's name, "" with no drive.
    [[nodiscard]] std::string traceTag() const { return runLevelText.empty() ? std::string() : " (" + runLevelText + ")"; }
    void afterSubmit(const AlignAnswers& asked);
    void trySubmit(double now);
    /// Hands the bound IRs to `fresh` by source; returns "" or a sentence the
    /// press appends to its status (an IR that could not be used, and why).
    [[nodiscard]] std::string supplyBoundIrs(AlignmentWizard& fresh) const;
};

}  // namespace rta::measure
