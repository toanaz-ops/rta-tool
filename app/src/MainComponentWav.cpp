// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See MainComponentWav.h.
#include "MainComponentWav.h"

#include "measure/WavFileSource.h"
#include "trace/TraceGroups.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>

namespace {

constexpr int kRowHeight = az::ui::touchTarget;

juce::String utf8(const std::string& text) { return juce::String::fromUTF8(text.c_str()); }

std::string fixed(double value, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
    return buffer;
}

void fillChannels(juce::ComboBox& combo, int count, int selected1Based) {
    combo.clear(juce::dontSendNotification);
    for (int i = 1; i <= count; ++i) combo.addItem("ch" + juce::String(i), i);
    combo.setSelectedId(selected1Based, juce::dontSendNotification);
}

}  // namespace

MainComponentWav::MainComponentWav(rta::trace::TraceLibrary& library) : library_(library) {
    loadButton_.getProperties().set(az::ui::hintProperty, "a WAV/AIFF recording of a reference and a measurement");
    loadButton_.onClick = [this] { loadClicked(); };
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
             &loadButton_, &fileLabel_, &refLegend_, &refCombo_, &measLegend_, &measCombo_, &startLegend_,
             &startEditor_, &lengthLegend_, &lengthEditor_, &analyseButton_, &readout_}) {
        parent.addAndMakeVisible(*c);
    }
}

void MainComponentWav::layout(juce::Rectangle<int> area) {
    const int gap = az::ui::gap;
    // Row 1: LOAD WAV and the file's own line, shaped like the STORE row above.
    auto row = area.removeFromTop(kRowHeight);
    const int buttonWidth = (row.getWidth() - gap) / 4;
    loadButton_.setBounds(row.removeFromLeft(buttonWidth));
    row.removeFromLeft(gap);
    fileLabel_.setBounds(row);
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
    const auto probe = rta::measure::WavFileSource::open(file);
    if (probe == nullptr) {
        file_ = juce::File();
        setControlsEnabled(false);
        fileLabel_.setText("not loaded", juce::dontSendNotification);
        say("wav: " + file.getFileName() + " could not be read (WAV and AIFF only)");
        return false;
    }
    file_ = file;
    const double fs = probe->sampleRate();
    const int channels = probe->numChannels();
    const double seconds = fs > 0.0 ? static_cast<double>(probe->lengthSamples()) / fs : 0.0;
    fileLabel_.setText(file.getFileName() + " - " + juce::String(channels) + " ch, " + utf8(fixed(fs, 0)) +
                           " Hz, " + utf8(fixed(seconds, 1)) + " s",
                       juce::dontSendNotification);
    fillChannels(refCombo_, channels, 1);
    fillChannels(measCombo_, channels, channels >= 2 ? 2 : 1);
    startEditor_.setText("0", false);
    // Floored to 10 ms so the prefilled "whole file" can never round past the end.
    lengthEditor_.setText(utf8(fixed(std::floor(seconds * 100.0) / 100.0, 2)), false);
    setControlsEnabled(channels >= 2);
    if (channels < 2) {
        rta::measure::WavTransferOutcome refusal;
        refusal.refusal = rta::measure::WavTransferRefusal::NotEnoughChannels;
        refusal.fileChannels = channels;
        say(utf8(rta::measure::wavTransferRefusalText(refusal)));
    } else {
        say("wav: " + juce::String(channels) + " channels, pick REF and MEAS, then ANALYSE");
    }
    return true;
}

void MainComponentWav::analyseClicked() {
    if (file_ == juce::File() || !analyseButton_.isEnabled()) return;
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
    if (auto outcome = job_.poll()) finish(*outcome);
}

void MainComponentWav::finish(rta::measure::WavTransferOutcome& out) {
    if (!out.ok()) {
        say(utf8(rta::measure::wavTransferRefusalText(out)));
        return;
    }
    const std::string name = out.trace->meta().deviceName;
    // `add` gives the trace the lowest free shade in group WAV, the way every other store does.
    if (library_.add(std::move(*out.trace), name, rta::trace::kWavGroup).empty()) {
        say("not stored -- duplicate capture id");
        return;
    }
    const double ms = out.sampleRate > 0.0 ? 1000.0 * out.delaySamples / out.sampleRate : 0.0;
    say("delay " + juce::String(out.delaySamples) + " samples (" + utf8(fixed(ms, 1)) + " ms), " +
        rta::measure::wavTransferTrustWord(out.trust) + " trust, " + utf8(fixed(out.effectiveAverages, 1)) +
        " averages -> stored " + utf8(name));
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
