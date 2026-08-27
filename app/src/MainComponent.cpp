// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.6 (Wave E / T10).
#include "MainComponent.h"

namespace {

// The legend gutter's own column, per Metrics.h: every panel that shares a
// legend column starts its fields at az::ui::gutterWidth, but that figure
// sizes a FIELD's legend, not a whole rail's width. This is the rail width
// itself -- wide enough for DevicePanel's widest row (a device name combo
// plus its gutter) and for ChannelRoleTable's index/name/role columns
// without either one clipping.
constexpr int kRailWidth = 360;

// Tall enough for DevicePanel's own content (caption, four field rows, the
// start/stop switch and the fault line -- see DevicePanel::resized()) with a
// little breathing room below rather than a height computed to the pixel:
// the panel fills whatever it is given and a hairline of empty panel face at
// the bottom costs nothing.
constexpr int kDevicePanelHeight = 300;

// SyntheticInput always writes two identical channels (see its header); a
// device panel is not involved, so this class names them itself.
const std::vector<std::string> kSyntheticChannelNames{"Synthetic L", "Synthetic R"};

}  // namespace

MainComponent::MainComponent()
    : analysisThread_(audioIo_.bus(), rta::measure::Analyser::Config{}),
      devicePanel_(audioIo_),
      channelRoleTable_(audioIo_.bus().config()),
      rtaView_(analysisThread_) {
    modeSwitch_.setClickingTogglesState(true);
    modeSwitch_.getProperties().set(az::ui::hintProperty, "no hardware needed");
    modeSwitch_.onClick = [this] { modeSwitchClicked(); };
    addAndMakeVisible(modeSwitch_);

    addAndMakeVisible(devicePanel_);
    addAndMakeVisible(channelRoleTable_);
    addAndMakeVisible(rtaView_);

    refreshChannelNamesFromDevice();

    // Slow poll: only watches for the device's own channel list changing
    // (a device opened, closed or swapped from devicePanel_) -- DevicePanel
    // and RtaView already run their own faster timers for the state that
    // actually needs one.
    startTimerHz(2);
}

MainComponent::~MainComponent() { stopTimer(); }

void MainComponent::setSyntheticMode(bool enabled) {
    if (enabled == isSyntheticMode()) {
        return;
    }

    if (enabled) {
        // CaptureBus::prepare() -- called from SyntheticInput's own
        // constructor -- has "no concurrent writer" as its precondition.
        // Stop any live device first so the real callback and the synthetic
        // thread never race the same bus.
        audioIo_.stop();
        devicePanel_.setEnabled(false);

        syntheticInput_ = std::make_unique<rta::measure::SyntheticInput>(
            audioIo_.bus(), rta::measure::SyntheticInput::Config{});

        // A default role so the plot has something to show the instant
        // synthetic mode engages, with no extra click needed on
        // channelRoleTable_ -- this IS the "the app demonstrably runs with
        // no hardware" requirement (plan §0 item 4). Channel index 0 carries
        // the same pink noise as index 1 (SyntheticInput writes identical
        // content to both channels; see its header), so Measurement on
        // either is equivalent.
        audioIo_.bus().config().setRole(0, rta::platform::ChannelRole::Measurement);

        lastChannelNames_ = kSyntheticChannelNames;
        channelRoleTable_.setChannelNames(lastChannelNames_);
    } else {
        // Order matters: destroy the synthetic writer before re-enabling the
        // panel that lets a user start a real one, so there is never a
        // window where both could be live at once.
        syntheticInput_.reset();
        devicePanel_.setEnabled(true);
        lastChannelNames_.clear();
        refreshChannelNamesFromDevice();
    }

    modeSwitch_.setToggleState(enabled, juce::dontSendNotification);
    repaint();
}

void MainComponent::modeSwitchClicked() {
    // setClickingTogglesState(true) flips the button's own toggle state
    // before onClick fires, so getToggleState() already reads the mode the
    // user just asked for.
    setSyntheticMode(modeSwitch_.getToggleState());
}

void MainComponent::timerCallback() {
    if (isSyntheticMode()) {
        return;  // fixed list, set once in setSyntheticMode()
    }
    refreshChannelNamesFromDevice();
}

void MainComponent::refreshChannelNamesFromDevice() {
    auto names = audioIo_.currentState().inputChannelNames;
    if (names != lastChannelNames_) {
        lastChannelNames_ = names;
        channelRoleTable_.setChannelNames(lastChannelNames_);
    }
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(az::ui::background);

    // Same two-piece masthead SpecimenComponent draws: the wordmark and its
    // qualifier want different weights, and a one-piece title needs a
    // non-ASCII dash, which is a source-encoding question this project has
    // no reason to open.
    auto masthead = mastheadArea_;
    auto rule = masthead.removeFromBottom(2);

    const auto brandFont = az::ui::legendFont(az::ui::switchFontSize, true, az::ui::trackingCaption);
    g.setColour(az::ui::text);
    g.setFont(brandFont);
    g.drawText("RTA TOOL", masthead, juce::Justification::centredLeft);

    masthead.removeFromLeft(static_cast<int>(az::ui::stringWidth(brandFont, "RTA TOOL")) + az::ui::gap * 3);
    g.setColour(az::ui::dim);
    g.setFont(az::ui::legendFont(az::ui::columnFontSize, false, az::ui::trackingColumn));
    g.drawText("REAL-TIME ANALYSER", masthead, juce::Justification::centredLeft);

    az::ui::drawEngravedDivider(g, rule);
}

void MainComponent::resized() {
    auto area = getLocalBounds().reduced(az::ui::gap * 2);

    mastheadArea_ = area.removeFromTop(az::ui::transportHeight / 2);
    area.removeFromTop(az::ui::gap * 2);

    auto rail = area.removeFromLeft(kRailWidth);
    area.removeFromLeft(az::ui::gap * 2);

    modeSwitch_.setBounds(rail.removeFromTop(az::ui::buttonCellHeight));
    rail.removeFromTop(az::ui::gap * 2);

    devicePanel_.setBounds(rail.removeFromTop(kDevicePanelHeight));
    rail.removeFromTop(az::ui::gap * 2);

    // Fills whatever is left of the rail -- a shrunk window trims rows off
    // the bottom of the channel list before it ever touches the plot.
    channelRoleTable_.setBounds(rail);

    rtaView_.setBounds(area);
}
