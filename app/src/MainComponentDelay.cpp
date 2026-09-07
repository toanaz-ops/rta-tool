// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. MainComponent.cpp's own L7-DELAY task F2
// split (record docs/dsp/2026-09-06-l7-auto-delay.md sec.11.2-11.4): the
// Locate/Apply methods, moved here to keep MainComponent.cpp under the
// project's 400-line cap -- the same reason Analyser.cpp has
// AnalyserPublish.cpp and TransferView.cpp has TransferViewDraw.cpp.
// Member-function definitions, declared in MainComponent.h, no different in
// kind from anything else in that class.
#include "MainComponent.h"

#include "measure/OutputPolicy.h"
#include "rta/gen/Noise.h"
#include "rta/gen/Prng.h"

void MainComponent::locateClicked() {
    if (locateWaitingForSettle_ || locateCaptureArmed_) {
        return;  // one Locate at a time, matching DelayLocator's own shape
    }
    // A fresh seed every click rather than a fixed one: two Locates back to
    // back should not correlate against each other's excitation by
    // coincidence, however small the chance.
    rta::gen::Pcg32 rng(static_cast<std::uint64_t>(juce::Time::currentTimeMillis()), 1);
    audioIo_.output().setSource(rta::gen::PinkNoise(rng, -12.0));
    rta::measure::soloOutput(audioIo_.output(), /*outputChannel=*/0);
    audioIo_.output().armSource();

    locateWaitingForSettle_ = true;
    delaySuggestion_.reset();
    applyButton_.setEnabled(false);
    delayReadout_.setText("delay: locating...", juce::dontSendNotification);
}

void MainComponent::pollLocatePipeline() {
    if (locateWaitingForSettle_) {
        // record sec.11: the first 10 ms are not stationary -- same 480
        // samples at 48 kHz DelayLocator::feedHop waits for (that class is
        // not used here because its feedHop() wants real per-hop audio,
        // which only AnalysisThread's drain loop sees; this class polls the
        // same OutputEngine telemetry that wait is built on instead).
        constexpr double kSettleSamplesAt48k = 480.0;
        const double settleSamples =
                kSettleSamplesAt48k * (audioIo_.output().sampleRate() / 48000.0);
        if (static_cast<double>(audioIo_.output().renderedSamples()) < settleSamples) {
            return;
        }
        constexpr std::size_t kCaptureLength = 8192;
        analysisThread_.armLocateCapture(/*routeIndex=*/0, kCaptureLength);
        locateWaitingForSettle_ = false;
        locateCaptureArmed_ = true;
    }

    if (!locateCaptureArmed_) {
        return;
    }
    const auto capture = analysisThread_.locateCapture();
    if (capture == nullptr || capture == lastHandledCapture_) {
        return;
    }
    lastHandledCapture_ = capture;
    // disarmSource() BEFORE suggestDelay (record sec.8): nothing past this
    // point should keep the excitation playing.
    audioIo_.output().disarmSource();
    locateCaptureArmed_ = false;

    rta::dsp::PhatOptions options;
    options.sampleRate = audioIo_.output().sampleRate();
    delaySuggestion_ = rta::dsp::suggestDelay(capture->reference, capture->measurement, options,
                                               rta::dsp::DelayPolicy{});
    updateDelayReadout();
}

void MainComponent::updateDelayReadout() {
    if (!delaySuggestion_.has_value()) {
        delayReadout_.setText("delay: --", juce::dontSendNotification);
        applyButton_.setEnabled(false);
        return;
    }
    const auto& suggestion = *delaySuggestion_;

    // Reading-out convention (CLAUDE.md): whole samples (never a fraction of
    // one -- one-hertz-style precision theatre), one-decimal milliseconds,
    // trust as a WORD never a bare float (record sec.11.4).
    const double msPerSample = 1000.0 / audioIo_.output().sampleRate();
    const double ms =
            (static_cast<double>(suggestion.best.delaySamples) + suggestion.best.subSample) *
            msPerSample;
    const juce::String trustWord =
        suggestion.trust >= 0.5 ? "good" : (suggestion.trust >= 0.15 ? "fair" : "poor");
    juce::String verdictWord;
    switch (suggestion.verdict) {
        case rta::dsp::DelayVerdict::Accepted: verdictWord = "accepted"; break;
        case rta::dsp::DelayVerdict::BelowFloor: verdictWord = "below floor, refused"; break;
        case rta::dsp::DelayVerdict::WindowEmpty: verdictWord = "no signal found"; break;
    }

    delayReadout_.setText(juce::String(suggestion.best.delaySamples) + " samples (" +
                               juce::String(ms, 1) + " ms) -- " + trustWord + " trust, " +
                               verdictWord,
                           juce::dontSendNotification);
    applyButton_.setEnabled(suggestion.verdict == rta::dsp::DelayVerdict::Accepted);
}

void MainComponent::applyClicked() {
    if (!delaySuggestion_.has_value() ||
        delaySuggestion_->verdict != rta::dsp::DelayVerdict::Accepted) {
        return;
    }
    // An explicit rebuild (record sec.1.6, sec.8) -- sub-sample is shown,
    // never applied: the engine's own compensation is an integer sample
    // offset by design (DualFftEngine.h).
    analysisThread_.applyReferenceDelay(static_cast<int>(delaySuggestion_->best.delaySamples));
    applyButton_.setEnabled(false);
}
