// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See MainComponentWav.h.
#include "MainComponentWav.h"

#include "measure/WavFileSource.h"
#include "MainComponentWavText.h"
#include "trace/TraceGroups.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>

namespace {

using namespace mainwav;

constexpr int kRowHeight = az::ui::touchTarget;

}  // namespace

MainComponentWav::MainComponentWav(rta::trace::TraceLibrary& library) : library_(library) {
    loadButton_.getProperties().set(az::ui::hintProperty, "a WAV/AIFF recording of a reference and a measurement");
    loadButton_.onClick = [this] { loadClicked(); };
    loadMeasButton_.getProperties().set(az::ui::hintProperty,
                                        "an optional second recording that holds the measurement (another device)");
    loadMeasButton_.onClick = [this] { loadMeasClicked(); };
    clearMeasButton_.getProperties().set(az::ui::hintProperty, "back to one file: reference and measurement in LOAD WAV");
    clearMeasButton_.onClick = [this] { clearMeasClicked(); };
    analyseButton_.getProperties().set(az::ui::hintProperty,
                                       "transfer function of the chosen range, stored as a WAV trace");
    analyseButton_.onClick = [this] { analyseClicked(); };
    startEditor_.setInputRestrictions(12, "0123456789.");
    startEditor_.setTextToShowWhenEmpty("seconds", az::ui::faded);
    lengthEditor_.setInputRestrictions(12, "0123456789.");
    lengthEditor_.setTextToShowWhenEmpty("to the end", az::ui::faded);
    fileLabel_.setJustificationType(juce::Justification::centredLeft);
    readout_.setJustificationType(juce::Justification::topLeft);
    for (auto* legend : {&refLegend_, &measLegend_, &startLegend_, &lengthLegend_}) {
        legend->setJustificationType(juce::Justification::centredLeft);
    }
    fileLabel_.setText("no file loaded", juce::dontSendNotification);
    say("wav: LOAD WAV to analyse a recording offline");
    setControlsEnabled(false);
}

void MainComponentWav::attachTo(juce::Component& parent) {
    for (juce::Component* c : std::initializer_list<juce::Component*>{
             &loadButton_, &loadMeasButton_, &clearMeasButton_, &fileLabel_, &refLegend_, &refCombo_, &measLegend_, &measCombo_, &startLegend_,
             &startEditor_, &lengthLegend_, &lengthEditor_, &analyseButton_, &readout_}) {
        parent.addAndMakeVisible(*c);
    }
}

void MainComponentWav::layout(juce::Rectangle<int> area) {
    const int gap = az::ui::gap;
    // Row 1: LOAD WAV / LOAD MEAS WAV / CLEAR MEAS. Row 2: the file line(s).
    auto row = area.removeFromTop(kRowHeight);
    const int buttonWidth = (row.getWidth() - gap) / 4;
    // LOAD MEAS WAV is the longest label, so it gets the widest third.
    const int usable = row.getWidth() - 2 * gap;
    loadButton_.setBounds(row.removeFromLeft(usable * 28 / 100));
    row.removeFromLeft(gap);
    loadMeasButton_.setBounds(row.removeFromLeft(usable * 42 / 100));
    row.removeFromLeft(gap);
    clearMeasButton_.setBounds(row);
    area.removeFromTop(gap);
    fileLabel_.setBounds(area.removeFromTop(kRowHeight));
    area.removeFromTop(gap);

    // Rows 2 and 3: a legend, a field, a legend, a field.
    const auto pair = [&](juce::Label& l1, juce::Component& c1, juce::Label& l2, juce::Component& c2) {
        auto r = area.removeFromTop(kRowHeight);
        const int legendWidth = 62;
        const int fieldWidth = (r.getWidth() - 2 * legendWidth - 3 * gap) / 2;
        l1.setBounds(r.removeFromLeft(legendWidth));
        r.removeFromLeft(gap);
        c1.setBounds(r.removeFromLeft(fieldWidth));
        r.removeFromLeft(gap);
        l2.setBounds(r.removeFromLeft(legendWidth));
        r.removeFromLeft(gap);
        c2.setBounds(r);
        area.removeFromTop(gap);
    };
    pair(refLegend_, refCombo_, measLegend_, measCombo_);
    pair(startLegend_, startEditor_, lengthLegend_, lengthEditor_);

    auto analyseRow = area.removeFromTop(kRowHeight);
    analyseButton_.setBounds(analyseRow.removeFromLeft(buttonWidth));
    area.removeFromTop(gap);
    readout_.setBounds(area.removeFromTop(2 * kRowHeight));
}

void MainComponentWav::setControlsEnabled(bool enabled) {
    for (juce::Component* c : std::initializer_list<juce::Component*>{&refCombo_, &measCombo_, &startEditor_,
                                                                      &lengthEditor_, &analyseButton_}) {
        c->setEnabled(enabled);
    }
}

void MainComponentWav::loadClicked() {
    chooser_ = std::make_unique<juce::FileChooser>("Load a WAV or AIFF recording", file_, "*.wav;*.aif;*.aiff");
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser_->launchAsync(flags, [this](const juce::FileChooser& fc) {
        const auto result = fc.getResult();
        if (result == juce::File{}) return;  // cancelled: nothing happens
        performLoad(result);
    });
}

bool MainComponentWav::performLoad(const juce::File& file) {
    // A new file ends whatever the old one was doing: its answer would be filed under the wrong name.
    job_.cancelAndDiscard();
    rta::measure::WavFileSource::OpenFailure why{};
    const auto probe = rta::measure::WavFileSource::open(file, &why);
    if (probe == nullptr) {
        file_ = juce::File();
        refChannels_ = 0;
        setControlsEnabled(false);
        fileLabel_.setText("not loaded", juce::dontSendNotification);
        say("wav: " + file.getFileName() +
            (why == rta::measure::WavFileSource::OpenFailure::NoSamples
                 ? " has no samples"
                 : " could not be read (WAV and AIFF only)"));
        return false;
    }
    file_ = file;
    const double fs = probe->sampleRate();
    const double seconds = fs > 0.0 ? static_cast<double>(probe->lengthSamples()) / fs : 0.0;
    refChannels_ = probe->numChannels();
    refRate_ = fs;
    refTimeReference_ = probe->timeReferenceSamples();
    refSummary_ = file.getFileName() + " - " + juce::String(refChannels_) + " ch, " + utf8(fixed(fs, 0)) +
                  " Hz, " + utf8(fixed(seconds, 1)) + " s";
    startEditor_.setText("0", false);
    // Floored to 10 ms so the prefilled "whole file" can never round past the end.
    lengthEditor_.setText(utf8(fixed(std::floor(seconds * 100.0) / 100.0, 2)), false);
    refreshFiles();
    return true;
}

void MainComponentWav::analyseClicked() {
    if (file_ == juce::File() || !analyseButton_.isEnabled()) return;
    if (measFile_ != juce::File()) {
        analysePair();
        return;
    }
    rta::measure::WavTransferRequest request;
    request.referenceChannel = refCombo_.getSelectedId() - 1;
    request.measurementChannel = measCombo_.getSelectedId() - 1;
    request.startSeconds = startEditor_.getText().getDoubleValue();
    request.lengthSeconds = lengthEditor_.getText().getDoubleValue();
    request.fileName = file_.getFileName().toStdString();
    say("analysing ...");
    job_.start(file_, std::move(request));
}

void MainComponentWav::poll() {
    if (!job_.busy()) return;  // nothing running: the timer has nothing to take
    if (auto outcome = job_.poll()) finish(*outcome);
}

void MainComponentWav::finish(rta::measure::WavJobOutcome& out) {
    if (!out.ok()) {
        say(utf8(out.pairMode ? rta::measure::wavPairRefusalText(out.pair)
                              : rta::measure::wavTransferRefusalText(out.single)));
        return;
    }
    auto& transfer = out.transfer();
    const std::string name = transfer.trace->meta().deviceName;
    const auto id = library_.add(std::move(*transfer.trace), name, rta::trace::kWavGroup);
    if (id.empty()) {
        say("not stored -- duplicate capture id");
        return;
    }
    // `add` numbers shades per group, so the first WAV trace would share shade 0 with the first
    // SWEEP/STORE trace on TRANSFER: take the lowest index unused across every entry instead.
    (void)library_.assignSharedShade(id);
    say(successText(transfer, out.pairMode ? &out.pair : nullptr, name));
}

void MainComponentWav::setControlsForTest(int ref, int meas, double start, double length) {
    refCombo_.setSelectedId(ref, juce::dontSendNotification);
    measCombo_.setSelectedId(meas, juce::dontSendNotification);
    startEditor_.setText(utf8(fixed(start, 3)), false);
    lengthEditor_.setText(utf8(fixed(length, 3)), false);
}

bool MainComponentWav::waitForJobForTest(int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    while (job_.busy() && juce::Time::getMillisecondCounter() < deadline) {
        poll();
        if (job_.busy()) juce::Thread::sleep(5);
    }
    return !job_.busy();
}
