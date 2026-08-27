// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.5 (Wave D / T9).
#include "view/DevicePanel.h"

#include <az_ui/az_ui.h>

#include <cmath>

namespace rta::view {

namespace {

constexpr int kFaultPollHz = 4;

// Dim red: a fault reads as "something is wrong" without competing with a
// real clip/danger light elsewhere in the app -- this line sits under
// controls the user is looking at anyway, not flashing for attention.
const juce::Colour kFaultColour = az::ui::danger.withAlpha(0.7f);

/// Whole hertz, no decimal -- CLAUDE.md "Reading out numbers". A sample
/// rate is the same unit and the same convention as every other Hz figure
/// in the app, even though this one is a device property, not a frequency
/// axis label.
juce::String formatHz(double hz) {
    return juce::String(static_cast<juce::int64>(std::llround(hz))) + " Hz";
}

juce::String formatSamples(int size) { return juce::String(size) + " samples"; }

void populateStringCombo(juce::ComboBox& combo, const std::vector<std::string>& items,
                          const std::string& selected) {
    combo.clear(juce::dontSendNotification);
    int selectedId = 0;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const int itemId = static_cast<int>(i) + 1;
        combo.addItem(items[i], itemId);
        if (items[i] == selected) selectedId = itemId;
    }
    combo.setSelectedId(selectedId, juce::dontSendNotification);
}

}  // namespace

DevicePanel::DevicePanel(rta::platform::AudioIo& audioIo) : audioIo_(audioIo) {
    addAndMakeVisible(deviceTypeCombo_);
    addAndMakeVisible(deviceCombo_);
    addAndMakeVisible(sampleRateCombo_);
    addAndMakeVisible(bufferSizeCombo_);
    addAndMakeVisible(startStopButton_);

    deviceTypeCombo_.onChange = [this] { deviceTypeChanged(); };
    deviceCombo_.onChange = [this] { deviceChanged(); };
    sampleRateCombo_.onChange = [this] { sampleRateChanged(); };
    bufferSizeCombo_.onChange = [this] { bufferSizeChanged(); };

    // Driven entirely by AudioIo::isRunning() after each click, never by
    // JUCE's own toggle bookkeeping -- start() can fail, and the switch
    // must show what actually happened, not what was requested.
    startStopButton_.setClickingTogglesState(false);
    startStopButton_.onClick = [this] { startStopClicked(); };

    refreshFromState();
    startTimerHz(kFaultPollHz);
}

DevicePanel::~DevicePanel() { stopTimer(); }

void DevicePanel::timerCallback() {
    const auto fault = audioIo_.lastFault();
    if (fault.sequence != lastPaintedFaultSequence_) {
        // A new fault (or the device recovering) usually means the device's
        // own state moved too -- M5's unplug closes it out from under
        // isRunning() -- so the whole panel gets a fresh read, not just the
        // fault line.
        refreshFromState();
        return;
    }

    const auto drops = audioIo_.bus().totalDrops();
    if (drops != lastPaintedDropCount_) {
        lastPaintedDropCount_ = drops;
        repaint();
    }
}

void DevicePanel::refreshFromState() {
    populating_ = true;

    const auto state = audioIo_.currentState();

    populateStringCombo(deviceTypeCombo_, audioIo_.availableDeviceTypeNames(), state.typeName);
    populateStringCombo(deviceCombo_, audioIo_.availableDeviceNames(), state.deviceName);

    // Rate/buffer combos match on the numeric value read back from the
    // device, not on formatted text -- see cachedRates_ / cachedBufferSizes_
    // in the header for why the handlers below index into a cache instead
    // of re-querying AudioIo.
    cachedRates_ = audioIo_.availableSampleRates();
    sampleRateCombo_.clear(juce::dontSendNotification);
    int rateSelectedId = 0;
    for (std::size_t i = 0; i < cachedRates_.size(); ++i) {
        const int itemId = static_cast<int>(i) + 1;
        sampleRateCombo_.addItem(formatHz(cachedRates_[i]), itemId);
        if (cachedRates_[i] == state.sampleRate) rateSelectedId = itemId;
    }
    sampleRateCombo_.setSelectedId(rateSelectedId, juce::dontSendNotification);

    cachedBufferSizes_ = audioIo_.availableBufferSizes();
    bufferSizeCombo_.clear(juce::dontSendNotification);
    int bufferSelectedId = 0;
    for (std::size_t i = 0; i < cachedBufferSizes_.size(); ++i) {
        const int itemId = static_cast<int>(i) + 1;
        bufferSizeCombo_.addItem(formatSamples(cachedBufferSizes_[i]), itemId);
        if (cachedBufferSizes_[i] == state.bufferSize) bufferSelectedId = itemId;
    }
    bufferSizeCombo_.setSelectedId(bufferSelectedId, juce::dontSendNotification);

    const bool running = audioIo_.isRunning();
    startStopButton_.setButtonText(running ? "STOP" : "START");
    startStopButton_.setToggleState(running, juce::dontSendNotification);

    populating_ = false;

    lastPaintedFaultSequence_ = audioIo_.lastFault().sequence;
    lastPaintedDropCount_ = audioIo_.bus().totalDrops();
    repaint();
}

void DevicePanel::deviceTypeChanged() {
    if (populating_) return;
    audioIo_.setDesiredDeviceType(deviceTypeCombo_.getText().toStdString());

    // AudioIo.h: applied on the NEXT start(), not immediately. If a device
    // is already open, "next start()" means restarting right now -- or the
    // combo would sit there showing a choice that has not actually
    // happened, exactly the lie currentState() exists to prevent. If
    // nothing is running yet, the desired type just waits for the user's
    // own Start press.
    if (audioIo_.isRunning()) {
        audioIo_.stop();
        audioIo_.start();
    }
    refreshFromState();
}

void DevicePanel::deviceChanged() {
    if (populating_) return;
    audioIo_.setDesiredDevice(deviceCombo_.getText().toStdString());
    if (audioIo_.isRunning()) {
        audioIo_.stop();
        audioIo_.start();
    }
    refreshFromState();
}

void DevicePanel::sampleRateChanged() {
    if (populating_) return;
    // setSampleRate() restarts the device itself on success (AudioIo.h); a
    // refusal leaves the previous rate in place, which refreshFromState()
    // below shows by snapping the combo back -- never by trusting what was
    // just picked.
    const int index = sampleRateCombo_.getSelectedItemIndex();
    if (index >= 0 && static_cast<std::size_t>(index) < cachedRates_.size()) {
        audioIo_.setSampleRate(cachedRates_[static_cast<std::size_t>(index)]);
    }
    refreshFromState();
}

void DevicePanel::bufferSizeChanged() {
    if (populating_) return;
    const int index = bufferSizeCombo_.getSelectedItemIndex();
    if (index >= 0 && static_cast<std::size_t>(index) < cachedBufferSizes_.size()) {
        audioIo_.setBufferSize(cachedBufferSizes_[static_cast<std::size_t>(index)]);
    }
    refreshFromState();
}

void DevicePanel::startStopClicked() {
    if (audioIo_.isRunning()) {
        audioIo_.stop();
    } else {
        audioIo_.start();
    }
    refreshFromState();
}

void DevicePanel::paint(juce::Graphics& g) {
    g.fillAll(az::ui::panel);

    auto area = getLocalBounds().reduced(az::ui::gap * 2);
    az::ui::drawCaption(g, "DEVICE", area.removeFromTop(az::ui::captionHeight), az::ui::dim);

    const auto legendFont = az::ui::legendFont(az::ui::columnFontSize, true, az::ui::trackingColumn);
    auto legend = [&](juce::Rectangle<int> bounds, const char* text) {
        g.setColour(az::ui::faded);
        g.setFont(legendFont);
        g.drawText(text, bounds, juce::Justification::centredLeft, false);
    };

    legend(typeLegendArea_, "TYPE");
    legend(deviceLegendArea_, "DEVICE");
    legend(rateLegendArea_, "RATE");
    legend(bufferLegendArea_, "BUFFER");

    // Drop counter: mono so the digits do not jitter as they count, and
    // coloured warn (not danger -- a drop is a symptom the callback
    // survived, not a fault) the moment it leaves zero.
    auto dropsArea = dropReadoutArea_;
    const auto dropsLegend = dropsArea.removeFromLeft(az::ui::inlineLegendWidth);
    legend(dropsLegend, "DROPS");

    const auto drops = audioIo_.bus().totalDrops();
    g.setColour(drops > 0 ? az::ui::warn : az::ui::dim);
    g.setFont(az::ui::monoFont(az::ui::readoutFontSize));
    g.drawText(juce::String(static_cast<juce::int64>(drops)), dropsArea,
               juce::Justification::centredLeft, false);

    const auto fault = audioIo_.lastFault();
    if (fault.kind != rta::platform::Fault::Kind::None) {
        g.setColour(kFaultColour);
        g.setFont(az::ui::monoFont(az::ui::hintFontSize));
        g.drawText(juce::String(fault.message), faultLineArea_, juce::Justification::centredLeft, false);
    }
}

void DevicePanel::resized() {
    auto area = getLocalBounds().reduced(az::ui::gap * 2);
    area.removeFromTop(az::ui::captionHeight);  // the caption itself is drawn in paint()

    auto layoutRow = [&](juce::Rectangle<int>& legendArea) {
        auto row = area.removeFromTop(az::ui::fieldHeight);
        legendArea = row.removeFromLeft(az::ui::gutterWidth);
        area.removeFromTop(az::ui::gap);
        return row;
    };

    deviceTypeCombo_.setBounds(layoutRow(typeLegendArea_));
    deviceCombo_.setBounds(layoutRow(deviceLegendArea_));

    // Rate and buffer share one row, split at the field grid's fixed
    // fraction so this row lines up with any other two-field row in the
    // same column (Metrics.h: "rows that split differently are exactly what
    // made an earlier panel look ragged").
    auto rateBufferRow = layoutRow(rateLegendArea_);
    const int rateWidth = static_cast<int>(static_cast<float>(rateBufferRow.getWidth()) * az::ui::fieldSplit);
    sampleRateCombo_.setBounds(rateBufferRow.removeFromLeft(rateWidth));
    bufferLegendArea_ = rateBufferRow.removeFromLeft(az::ui::inlineLegendWidth);
    bufferSizeCombo_.setBounds(rateBufferRow);

    area.removeFromTop(az::ui::gap);
    auto switchRow = area.removeFromTop(az::ui::buttonCellHeight);
    startStopButton_.setBounds(switchRow.removeFromLeft(az::ui::buttonCellWidth));
    switchRow.removeFromLeft(az::ui::gap * 2);
    dropReadoutArea_ = switchRow.withSizeKeepingCentre(switchRow.getWidth(), az::ui::fieldHeight);

    area.removeFromTop(az::ui::gap * 2);
    faultLineArea_ = area.removeFromTop(az::ui::fieldHeight);
}

}  // namespace rta::view
