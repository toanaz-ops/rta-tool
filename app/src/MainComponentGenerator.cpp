// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See MainComponentGenerator.h.
#include "MainComponentGenerator.h"

#include <az_ui/az_ui.h>

#include <utility>

namespace {

// Fast enough that a 480-sample (10 ms) ramp-out is noticed within a couple of
// frames and a swap's hole stays near its 2-ramp floor; slow enough to cost
// nothing. It runs only while the runner is waiting on the audio callback.
constexpr int kFastPollMs = 10;

}  // namespace

MainComponentGenerator::MainComponentGenerator(
    rta::platform::OutputEngine& engine, std::function<rta::measure::GeneratorEnvironment()> environment)
    : runner_(engine, rta::measure::GeneratorHost{std::move(environment),
                                                  [] { return juce::Time::getMillisecondCounterHiRes(); }}) {
    genButton_.getProperties().set(az::ui::hintProperty,
                                   "pink noise, out 1, -30.0 dBFS RMS to start; never above -12.0");
    genButton_.onClick = [this] { genClicked(); };
    minusButton_.getProperties().set(az::ui::hintProperty, "generator level down 1.0 dB (floor -60.0)");
    minusButton_.onClick = [this] { stepClicked(-1); };
    plusButton_.getProperties().set(az::ui::hintProperty, "generator level up 1.0 dB (ceiling -12.0)");
    plusButton_.onClick = [this] { stepClicked(+1); };
    readout_.setJustificationType(juce::Justification::centredLeft);
    refresh();
}

MainComponentGenerator::~MainComponentGenerator() { stopTimer(); }

void MainComponentGenerator::attachTo(juce::Component& parent) {
    parent.addAndMakeVisible(genButton_);
    parent.addAndMakeVisible(minusButton_);
    parent.addAndMakeVisible(plusButton_);
    parent.addAndMakeVisible(readout_);
}

void MainComponentGenerator::layout(juce::Rectangle<int> strip) {
    constexpr int kGenWidth = 84;   // "STOP" / "GEN" with its hint line
    constexpr int kStepWidth = 40;  // "-" / "+"
    genButton_.setBounds(strip.removeFromLeft(kGenWidth));
    strip.removeFromLeft(az::ui::gap);
    minusButton_.setBounds(strip.removeFromLeft(kStepWidth));
    strip.removeFromLeft(az::ui::gap);
    plusButton_.setBounds(strip.removeFromLeft(kStepWidth));
    strip.removeFromLeft(az::ui::gap);
    readout_.setBounds(strip);
}

void MainComponentGenerator::genClicked() {
    if (runner_.active()) {
        runner_.stop();
    } else {
        static_cast<void>(runner_.start());  // a refusal is read back through the readout
    }
    refresh();
}

void MainComponentGenerator::stepClicked(int steps) {
    rta::measure::GeneratorSettings next = runner_.settings();
    next.levelDb = rta::measure::stepGeneratorLevelDb(next.levelDb, steps);
    static_cast<void>(runner_.apply(std::move(next)));
    refresh();
}

void MainComponentGenerator::poll() {
    runner_.poll();
    refresh();
}

void MainComponentGenerator::cancel(const std::string& reason) {
    runner_.cancel(reason);
    refresh();
}

void MainComponentGenerator::refresh() {
    using rta::measure::GeneratorState;
    const GeneratorState state = runner_.state();
    genButton_.setButtonText(state == GeneratorState::Idle ? "GEN" : "STOP");
    genButton_.setEnabled(state != GeneratorState::Stopping);
    const juce::String text = runner_.readout();
    if (text != readout_.getText()) readout_.setText(text, juce::dontSendNotification);

    // Swapping and Stopping wait on the audio callback: poll fast until they
    // resolve. Playing and Idle need only the 2 Hz tick (epoch / device).
    if (runner_.needsFastPoll()) {
        if (!isTimerRunning()) startTimer(kFastPollMs);
    } else if (isTimerRunning()) {
        stopTimer();
    }
}
