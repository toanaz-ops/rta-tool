// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. H12 T4 (docs/dsp/2026-11-10-g22b-two-file-wav-drift.md E2, E9): the
// two-file half of MainComponentWav -- LOAD MEAS WAV, CLEAR MEAS, the file/channel/readout
// refresh both files share, ANALYSE on a pair and the success sentence. See MainComponentWav.h.
//
// Message thread only. LOAD opens a short-lived reader to learn each file's header (channels,
// rate, length, `bext` time reference); the worker (WavTransferJob) opens its own.
#include "MainComponentWav.h"

#include "MainComponentWavText.h"
#include "measure/WavFileSource.h"

#include <utility>

using namespace mainwav;

void MainComponentWav::loadMeasClicked() {
    chooser_ = std::make_unique<juce::FileChooser>("Load the measurement recording (WAV or AIFF)", measFile_ != juce::File() ? measFile_ : file_,
                                                   "*.wav;*.aif;*.aiff");
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser_->launchAsync(flags, [this](const juce::FileChooser& fc) {
        const auto result = fc.getResult();
        if (result == juce::File{}) return;  // cancelled: nothing happens
        performLoadMeas(result);
    });
}

bool MainComponentWav::performLoadMeas(const juce::File& file) {
    if (file_ == juce::File()) {
        say("wav: LOAD WAV first, then LOAD MEAS WAV adds the measurement file");
        return false;
    }
    // A new file ends whatever the old one was doing: its answer would be filed under the wrong name.
    job_.cancelAndDiscard();
    if (file == file_) {
        // The same recording twice is the one-file path: its channels already hold both signals.
        measFile_ = juce::File();
        refreshFiles();
        return true;
    }
    rta::measure::WavFileSource::OpenFailure why{};
    const auto probe = rta::measure::WavFileSource::open(file, &why);
    if (probe == nullptr) {
        say("wav: " + file.getFileName() +
            (why == rta::measure::WavFileSource::OpenFailure::NoSamples
                 ? " has no samples"
                 : " could not be read (WAV and AIFF only)"));
        return false;
    }
    const double fs = probe->sampleRate();
    const double seconds = fs > 0.0 ? static_cast<double>(probe->lengthSamples()) / fs : 0.0;
    measFile_ = file;
    measChannels_ = probe->numChannels();
    measTimeReference_ = probe->timeReferenceSamples();
    measSummary_ = file.getFileName() + " - " + juce::String(measChannels_) + " ch, " + utf8(fixed(fs, 0)) +
                   " Hz, " + utf8(fixed(seconds, 1)) + " s";
    refreshFiles();
    return true;
}

void MainComponentWav::clearMeasClicked() {
    job_.cancelAndDiscard();
    measFile_ = juce::File();
    measChannels_ = 0;
    measSummary_ = {};
    measTimeReference_.reset();
    if (file_ != juce::File()) refreshFiles();
}

void MainComponentWav::refreshFiles() {
    const bool pair = measFile_ != juce::File();
    fileLabel_.setText(pair ? refSummary_ + " | " + measSummary_ : refSummary_, juce::dontSendNotification);
    fillChannels(refCombo_, refChannels_, 1);
    if (pair) {
        fillChannels(measCombo_, measChannels_, 1);
    } else {
        fillChannels(measCombo_, refChannels_, refChannels_ >= 2 ? 2 : 1);
    }
    // A pair needs one channel in each file; one file needs two channels of its own.
    setControlsEnabled(pair || refChannels_ >= 2);
    if (pair) {
        juce::String text = "wav: two files, pick REF from " + file_.getFileName() + " and MEAS from " +
                            measFile_.getFileName() + ", then ANALYSE";
        if (refTimeReference_.has_value() && measTimeReference_.has_value() && refRate_ > 0.0) {
            const auto prior = rta::measure::bextPriorSamples(refTimeReference_, measTimeReference_);
            text += " (bext start offset " + utf8(signedFixed(static_cast<double>(prior) / refRate_, 3)) + " s)";
        }
        say(text);
    } else if (refChannels_ < 2) {
        rta::measure::WavTransferOutcome refusal;
        refusal.refusal = rta::measure::WavTransferRefusal::NotEnoughChannels;
        refusal.fileChannels = refChannels_;
        say(utf8(rta::measure::wavTransferRefusalText(refusal)));
    } else {
        say("wav: " + juce::String(refChannels_) + " channels, pick REF and MEAS, then ANALYSE");
    }
}

void MainComponentWav::analysePair() {
    rta::measure::WavPairJobRequest job;
    job.reference = file_;
    job.measurement = measFile_;
    job.referenceChannel = refCombo_.getSelectedId() - 1;
    job.measurementChannel = measCombo_.getSelectedId() - 1;
    job.request.startSeconds = startEditor_.getText().getDoubleValue();
    job.request.lengthSeconds = lengthEditor_.getText().getDoubleValue();
    job.request.referenceFileName = file_.getFileName().toStdString();
    job.request.measurementFileName = measFile_.getFileName().toStdString();
    say("analysing ...");
    job_.start(std::move(job));
}

juce::String MainComponentWav::successText(const rta::measure::WavTransferOutcome& out,
                                           const rta::measure::WavPairOutcome* pair,
                                           const std::string& name) const {
    juce::String text = "delay " + juce::String(out.delaySamples) + " samples (" +
                        utf8(rta::measure::wavTransferDelayMsText(out.delaySamples, out.sampleRate)) + " ms), " +
                        rta::measure::wavTransferTrustWord(out.trust) + " trust, " +
                        utf8(fixed(out.effectiveAverages, 1)) + " averages";
    if (pair != nullptr) {
        text += pair->sharedClock ? juce::String(", shared clock")
                                  : ", clock " + utf8(signedFixed(pair->ppm, 2)) + " ppm corrected";
        text += ", offset " + utf8(fixed(pair->offsetSeconds, 3)) + " s";
    }
    return text + " -> stored " + utf8(name);
}
