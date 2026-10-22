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

/// L7-DELAY task F2 (record docs/dsp/2026-09-06-l7-auto-delay.md
/// sec.11.2-11.4). LOCATE: pink noise, strict solo on output channel 0
/// (record sec.3, DEL-R4 -- a Locate is a measurement action), armSource;
/// the settle wait and the capture itself run on `analysisThread_`
/// (armLocateCapture) and are polled from `pollLocatePipeline()`, not
/// blocked on here. APPLY: rebuilds every live Analyser with the found
/// delay (record sec.1.6, sec.8 -- an explicit rebuild, never a live
/// nudge), enabled only once a suggestion reads `Accepted`.
void MainComponent::locateClicked() {
    // L7-EQ VERIFY plays its own pink noise through the same output engine and
    // LOCATE disarms at the end of its capture, so an overlap would silently
    // kill VERIFY's excitation (H7: setSource's refusal below is honoured too).
    // L6a Wave 3: calibrationCaptureArmed_ and SWEEP share the SAME
    // accumulator (MainComponentCalibration.cpp's own comment) -- refuse
    // rather than stomp on an in-flight capture. Only VERIFY gets a message;
    // the rest is the one-Locate-at-a-time silence the deleted DelayLocator
    // class had (see pollLocatePipeline's own comment below).
    if (outputBusy()) {
        if (generator_.active()) {
            delayReadout_.setText("delay: refused -- GEN is playing; press STOP", juce::dontSendNotification);
        } else if (eq_.verifyBusy()) {
            delayReadout_.setText("delay: refused -- VERIFY is running on the output", juce::dontSendNotification);
        }
        return;
    }
    // A fresh seed every click rather than a fixed one: two Locates back to
    // back should not correlate against each other's excitation by
    // coincidence, however small the chance.
    rta::gen::Pcg32 rng(static_cast<std::uint64_t>(juce::Time::currentTimeMillis()), 1);
    // H7 T13: the level is the generator's setting in FOLLOW GEN (dBFS RMS, the
    // noise's own convention) and the outputs are its ticked ones; OWN keeps
    // -12 dBFS and out 1 (`measurementDrive` answers for both).
    const rta::measure::MeasurementDrive drive = measurementDrive();
    const std::vector<int> outputs = drive.outputs.empty() ? std::vector<int>{0} : drive.outputs;
    locateDriveText_ = rta::measure::measurementDriveText(drive.levelDb, rta::measure::LevelConvention::Rms, outputs);
    // setSource's bool is the refusal (output-path record sec.6). Ignoring it
    // armed whatever source was still installed -- after a GEN stop that timed
    // out on a stopped device, GEN's own -30 dBFS pink -- as if it were LOCATE's.
    if (!audioIo_.output().setSource(rta::gen::PinkNoise(rng, drive.levelDb))) {
        delayReadout_.setText("delay: refused -- the output is still playing a source",
                              juce::dontSendNotification);
        return;
    }
    rta::measure::routeOutputSet(audioIo_.output(), outputs);
    audioIo_.output().armSource();

    locateWaitingForSettle_ = true;
    delaySuggestion_.reset();
    applyButton_.setEnabled(false);
    delayReadout_.setText("delay: locating @ " + juce::String(locateDriveText_) + "...", juce::dontSendNotification);
}

/// Drops a LOCATE waiting for settle or capture and disarms its source
/// (called where the input mode changes). No-op when idle.
void MainComponent::cancelLocate() {
    if (!locateWaitingForSettle_ && !locateCaptureArmed_) {
        return;
    }
    audioIo_.output().disarmSource();
    locateWaitingForSettle_ = false;
    locateCaptureArmed_ = false;
    delaySuggestion_.reset();
    updateDelayReadout();
}

/// Every `timerCallback()` tick: advances the Locate state machine (settle
/// -> arm -> read the capture -> suggestDelay -> disarm), never blocking.
void MainComponent::pollLocatePipeline() {
    if (locateWaitingForSettle_) {
        // record sec.11: the first 10 ms are not stationary -- 480 samples
        // at 48 kHz. lane L7-DELAY task F1 originally built a JUCE-free
        // DelayLocator class for this whole Locate sequence, but its
        // feedHop() wanted real per-hop audio that only AnalysisThread's
        // drain loop sees, so it was never wired to a caller; DelayLocator
        // was deleted as superseded, and this method polls the same
        // OutputEngine telemetry that wait was built on instead, calling
        // rta::dsp::suggestDelay directly (see below) rather than through
        // that class.
        constexpr double kSettleSamplesAt48k = 480.0;
        const double settleSamples =
                kSettleSamplesAt48k * (audioIo_.output().sampleRate() / 48000.0);
        if (static_cast<double>(audioIo_.output().renderedSamples()) < settleSamples) {
            return;
        }
        // Not while the input is mid-resync: that gate discards what the
        // capture would record, and its drain would then abort the capture.
        if (analysisThread_.resyncPending()) {
            return;
        }
        constexpr std::size_t kCaptureLength = 8192;
        analysisThread_.armLocateCapture(/*routeIndex=*/0, kCaptureLength);
        locateResync_ = analysisThread_.resyncSequence();
        locateDrops_ = analysisThread_.droppedFrames();
        locateWaitingForSettle_ = false;
        locateCaptureArmed_ = true;
    }

    if (!locateCaptureArmed_) {
        return;
    }
    // A resync (device or channel-role change) during the capture discards
    // samples the delay is measured from: the answer would be shifted, silently.
    if (analysisThread_.resyncSequence() != locateResync_) {
        cancelLocate();
        delayReadout_.setText("delay: aborted -- the input resynced during the capture",
                              juce::dontSendNotification);
        return;
    }
    // Frames the bus refused (ring full) leave a hole in the capture's timeline, and the
    // delay is read off that timeline. Capture pointer first, count after (acquire order),
    // so a drop in the last hop cannot be missed; the same abort as the SWEEP's.
    const auto capture = analysisThread_.locateCapture();
    if (const auto lost = analysisThread_.droppedFrames(); lost != locateDrops_) {
        cancelLocate();
        delayReadout_.setText("delay: aborted -- the input dropped frames during the capture, try again",
                              juce::dontSendNotification);
        return;
    }
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

    // H7 T13: a quiet drive shows as a quiet reading, never an unexplained one.
    delayReadout_.setText(juce::String(suggestion.best.delaySamples) + " samples (" +
                               juce::String(ms, 1) + " ms) -- " + trustWord + " trust, " +
                               verdictWord + " @ " + juce::String(locateDriveText_),
                           juce::dontSendNotification);
    applyButton_.setEnabled(suggestion.verdict == rta::dsp::DelayVerdict::Accepted);
}

void MainComponent::applyClicked() {
    if (!delaySuggestion_.has_value() ||
        delaySuggestion_->verdict != rta::dsp::DelayVerdict::Accepted) {
        return;
    }
    // APPLY rebuilds every analyser with a new delay, which would change
    // `appliedDelaySamples` between ALIGN's two captures and leave them on
    // different delay corrections (ALIGN-UI decision 12).
    if (align_.busy()) {
        delayReadout_.setText("delay: refused -- ALIGN is running", juce::dontSendNotification);
        return;
    }
    // An explicit rebuild (record sec.1.6, sec.8) -- sub-sample is shown,
    // never applied: the engine's own compensation is an integer sample
    // offset by design (DualFftEngine.h).
    analysisThread_.applyReferenceDelay(static_cast<int>(delaySuggestion_->best.delaySamples));
    applyButton_.setEnabled(false);
}
