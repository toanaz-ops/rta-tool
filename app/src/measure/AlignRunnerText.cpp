// SPDX-License-Identifier: AGPL-3.0-or-later
// The words AlignRunner says: block, refusal and source names. Split out of
// AlignRunner.cpp to keep it under the 400-line cap; no state lives here.
#include "measure/AlignRunnerText.h"

namespace rta::measure {
namespace detail {

const char* sourceLabel(Source source) { return source == Source::Main ? "MAIN" : "SUB"; }

const char* wizardRefusalName(WizardRefusal refusal) {
    switch (refusal) {
        case WizardRefusal::None: return "none";
        case WizardRefusal::MissingAnswer: return "a question is unanswered";
        case WizardRefusal::EngineNotQuiescent: return "the output engine is not quiescent";
        case WizardRefusal::WindowNotOpen: return "the capture window is not open";
        case WizardRefusal::WrongStep: return "wrong step";
        case WizardRefusal::SampleRateMismatch: return "the two captures differ in sample rate";
        case WizardRefusal::FftSizeMismatch: return "the two captures differ in fftSize";
        case WizardRefusal::ReferenceMismatch: return "the two captures differ in channel roles";
        case WizardRefusal::TraceNotUsable: return "a capture has no phase to fit";
        case WizardRefusal::Count: break;
    }
    return "";
}

const char* fitRefusalName(rta::dsp::CrossoverRefusal refusal) {
    switch (refusal) {
        case rta::dsp::CrossoverRefusal::None: return "none";
        case rta::dsp::CrossoverRefusal::AllBinsAbsent: return "no bin had coherence";
        case rta::dsp::CrossoverRefusal::NoCrossing: return "the two magnitudes never cross";
        case rta::dsp::CrossoverRefusal::TooFewBins: return "too few bins in the fit window";
        case rta::dsp::CrossoverRefusal::RangeEmpty: return "the fit window holds no bin";
    }
    return "";
}

}  // namespace detail

std::string alignBlockText(AlignBlock kind) {
    switch (kind) {
        case AlignBlock::None: return "";
        case AlignBlock::NotAttached: return "ALIGN is not wired to an output engine";
        case AlignBlock::Running: return "ALIGN is already running";
        case AlignBlock::MissingAnswer: return "answer all four questions first";
        case AlignBlock::SameOutput: return "MAIN and SUB are the same output: pick two different outputs";
        case AlignBlock::OutputMissing: return "an output you picked does not exist on this device";
        case AlignBlock::DeviceNotRunning:
            return "the audio device is not running: open it in the device panel, or switch to SYNTHETIC";
        case AlignBlock::CaptureBusy:
            return "GEN, LOCATE, CAL, SWEEP or VERIFY is running and owns the output or the capture: "
                   "wait for it to finish (press STOP if it is GEN)";
        case AlignBlock::NoTransfer: return "no TRANSFER measurement is published: feed a reference channel";
        case AlignBlock::SampleRateMismatch: return "input and output sample rates differ";
        case AlignBlock::ExponentialAveraging:
            return "transfer averaging is Exponential: its memory never fully clears, so no dwell "
                   "guarantees a clean per-output estimate; switch to Fifo";
        case AlignBlock::EngineNotQuiescent:
            return "the output engine is not quiescent: another excitation is running or still ramping out";
    }
    return "";
}

}  // namespace rta::measure
