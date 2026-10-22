// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See MainComponentGenerator.h.
#include "MainComponentGenerator.h"

#include <az_ui/az_ui.h>

#include "rta/platform/ChannelConfig.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace {

// Fast enough that a 480-sample (10 ms) ramp-out is noticed within a couple of
// frames and a swap's hole stays near its 2-ramp floor; slow enough to cost
// nothing. It runs only while the runner is waiting on the audio callback.
constexpr int kFastPollMs = 10;

constexpr int kSignalPink = 1, kSignalWhite = 2, kSignalSine = 3;

int signalId(rta::measure::GeneratorSignal signal) {
    switch (signal) {
        case rta::measure::GeneratorSignal::White: return kSignalWhite;
        case rta::measure::GeneratorSignal::Sine: return kSignalSine;
        case rta::measure::GeneratorSignal::Pink: break;
    }
    return kSignalPink;
}

}  // namespace

MainComponentGenerator::MainComponentGenerator(
    rta::platform::OutputEngine& engine, std::function<rta::measure::GeneratorEnvironment()> environment)
    : runner_(engine, rta::measure::GeneratorHost{environment,
                                                  [] { return juce::Time::getMillisecondCounterHiRes(); }}),
      environment_(std::move(environment)) {
    genButton_.getProperties().set(az::ui::hintProperty, "start / stop the generator; never above -12.0 dBFS");
    genButton_.onClick = [this] { genClicked(); };
    // 44 px (touch target) high, with a drawn glyph: see StepButton in the header.
    minusButton_.setButtonText("-");
    plusButton_.setButtonText("+");
    minusButton_.getProperties().set(az::ui::hintProperty, "generator level down 1.0 dB (floor -60.0)");
    minusButton_.onClick = [this] { stepClicked(-1); };
    plusButton_.getProperties().set(az::ui::hintProperty, "generator level up 1.0 dB (ceiling -12.0)");
    plusButton_.onClick = [this] { stepClicked(+1); };

    signalCombo_.addItem("PINK", kSignalPink);
    signalCombo_.addItem("WHITE", kSignalWhite);
    signalCombo_.addItem("SINE", kSignalSine);
    signalCombo_.onChange = [this] {
        const int id = signalCombo_.getSelectedId();
        signalSelected(id == kSignalWhite  ? rta::measure::GeneratorSignal::White
                       : id == kSignalSine ? rta::measure::GeneratorSignal::Sine
                                           : rta::measure::GeneratorSignal::Pink);
    };
    for (std::size_t i = 0; i < rta::measure::kSineFrequenciesHz.size(); ++i) {
        frequencyCombo_.addItem(rta::measure::frequencyText(rta::measure::kSineFrequenciesHz[i]),
                                static_cast<int>(i) + 1);
    }
    frequencyCombo_.onChange = [this] {
        const int index = frequencyCombo_.getSelectedId() - 1;
        if (index >= 0 && index < static_cast<int>(rta::measure::kSineFrequenciesHz.size())) {
            frequencySelected(rta::measure::kSineFrequenciesHz[static_cast<std::size_t>(index)]);
        }
    };
    outButton_.getProperties().set(az::ui::hintProperty, "outputs the generator plays on");
    outButton_.onClick = [this] { showOutputMenu(); };
    readout_.setJustificationType(juce::Justification::centredLeft);
    refresh();
}

MainComponentGenerator::~MainComponentGenerator() { stopTimer(); }

void MainComponentGenerator::StepButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    getLookAndFeel().drawButtonBackground(
        g, *this, findColour(getToggleState() ? buttonOnColourId : buttonColourId), highlighted, down);
    // 16 x 2.5 px bars centred on the face: a minus is the horizontal one, a plus adds the vertical.
    constexpr float kHalfLength = 8.0f;
    constexpr float kThickness = 2.5f;
    const auto centre = getLocalBounds().toFloat().getCentre();
    g.setColour(az::ui::text.withAlpha(isEnabled() ? 1.0f : 0.4f));
    g.fillRect(juce::Rectangle<float>(2.0f * kHalfLength, kThickness).withCentre(centre));
    if (plus_) g.fillRect(juce::Rectangle<float>(kThickness, 2.0f * kHalfLength).withCentre(centre));
}

void MainComponentGenerator::attachTo(juce::Component& parent) {
    for (juce::Component* child : std::initializer_list<juce::Component*>{
             &genButton_, &minusButton_, &plusButton_, &signalCombo_, &frequencyCombo_, &outButton_, &readout_}) {
        parent.addAndMakeVisible(*child);
    }
}

void MainComponentGenerator::layout(juce::Rectangle<int> strip) {
    constexpr int kGenWidth = 84;     // "STOP" / "GEN"
    constexpr int kStepWidth = az::ui::touchTarget;  // "-" / "+": a finger-sized target
    constexpr int kSignalWidth = 96;  // "WHITE" and the combo's arrow
    constexpr int kFrequencyWidth = 104;  // "20000 Hz"
    constexpr int kOutWidth = 64;
    const auto next = [&](int width) {
        auto cell = strip.removeFromLeft(width);
        strip.removeFromLeft(az::ui::gap);
        return cell;
    };
    genButton_.setBounds(next(kGenWidth));
    minusButton_.setBounds(next(kStepWidth));
    plusButton_.setBounds(next(kStepWidth));
    signalCombo_.setBounds(next(kSignalWidth).withSizeKeepingCentre(kSignalWidth, az::ui::fieldHeight));
    frequencyCombo_.setBounds(next(kFrequencyWidth).withSizeKeepingCentre(kFrequencyWidth, az::ui::fieldHeight));
    outButton_.setBounds(next(kOutWidth));
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

void MainComponentGenerator::signalSelected(rta::measure::GeneratorSignal signal) {
    rta::measure::GeneratorSettings next = runner_.settings();
    next.signal = signal;
    static_cast<void>(runner_.apply(std::move(next)));  // while playing: one swap
    refresh();
}

void MainComponentGenerator::frequencySelected(int hz) {
    rta::measure::GeneratorSettings next = runner_.settings();
    next.frequencyHz = static_cast<double>(hz);
    static_cast<void>(runner_.apply(std::move(next)));
    refresh();
}

void MainComponentGenerator::outputToggled(int channel) {
    rta::measure::GeneratorSettings next = runner_.settings();
    auto& outputs = next.outputs;
    const auto found = std::find(outputs.begin(), outputs.end(), channel);
    if (found != outputs.end()) {
        outputs.erase(found);
    } else {
        outputs.push_back(channel);
        std::sort(outputs.begin(), outputs.end());
    }
    // An empty list is refused (`LastOutput`) and nothing changes: a generator
    // always has somewhere to play. Live routing, no swap.
    static_cast<void>(runner_.apply(std::move(next)));
    refresh();
}

std::vector<std::pair<std::string, bool>> MainComponentGenerator::outputChoices() const {
    const auto& ticked = runner_.settings().outputs;
    const int reported = environment_ ? environment_().numOutputs : 0;
    // Unknown (LIVE, no device yet): offer what is already ticked, so the menu
    // never hides the generator's own selection.
    const int count = reported > 0 ? std::min(reported, rta::platform::kMaxChannels)
                                   : std::max(1, ticked.empty() ? 1 : ticked.back() + 1);
    std::vector<std::pair<std::string, bool>> rows;
    for (int ch = 0; ch < count; ++ch) {
        rows.emplace_back("out " + std::to_string(ch + 1), std::find(ticked.begin(), ticked.end(), ch) != ticked.end());
    }
    return rows;
}

void MainComponentGenerator::showOutputMenu() {
    const auto rows = outputChoices();
    const auto tickedCount = std::count_if(rows.begin(), rows.end(), [](const auto& r) { return r.second; });
    juce::PopupMenu menu;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        // The last tick stays: its item is greyed rather than refused on click.
        const bool removable = !(rows[i].second && tickedCount <= 1);
        menu.addItem(static_cast<int>(i) + 1, rows[i].first, removable, rows[i].second);
    }
    juce::Component::SafePointer<juce::TextButton> alive(&outButton_);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&outButton_), [this, alive](int picked) {
        if (alive != nullptr && picked > 0) outputToggled(picked - 1);
    });
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
    const auto& settings = runner_.settings();
    signalCombo_.setSelectedId(signalId(settings.signal), juce::dontSendNotification);
    const auto& hz = rta::measure::kSineFrequenciesHz;
    const auto nearest = std::min_element(hz.begin(), hz.end(), [&](int a, int b) {
        return std::abs(a - settings.frequencyHz) < std::abs(b - settings.frequencyHz);
    });
    frequencyCombo_.setSelectedId(static_cast<int>(nearest - hz.begin()) + 1, juce::dontSendNotification);
    frequencyCombo_.setEnabled(settings.signal == rta::measure::GeneratorSignal::Sine);
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
