// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. H7 tasks T3/T6/T7
// (docs/plans/2026-10-21-h7-generator-output-plan.md): the masthead's generator
// controller. GEN toggles the generator (it reads STOP while one is running),
// - and + step the level 1.0 dB, SIGNAL picks pink / white / sine, FREQ picks
// the sine's frequency, OUT picks the outputs it plays on, and a readout says
// what is playing. The state machine, the ceiling, the swap and every refusal
// live in rta::measure::GeneratorRunner / GeneratorModel (JUCE-free, proven
// with no device); this class only owns the widgets, forwards presses, and
// keeps a 100 Hz poll running while the runner is waiting on the audio
// callback.
//
// T13: the generator's SETTINGS are also what a measurement follows (owner
// decision D1). `measurementDrive` is the one answer to "what level and
// outputs does a measurement use right now?"; the mode it answers for is a
// member here so the OWN toggle of wave B2 lands in this class and nowhere
// else.
//
// Nothing here persists: every launch is pink, -30.0 dBFS, out 1, stopped
// (owner decision D3).
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "measure/GeneratorRunner.h"
#include "measure/MeasurementDrive.h"
#include "rta/platform/OutputEngine.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

class MainComponentGenerator final : private juce::Timer {
public:
    /// `environment` is what only the composition root knows: SYNTHETIC or a
    /// running device, the output count, and which measurement (if any)
    /// holds the output. `engine` is borrowed and must outlive this object.
    MainComponentGenerator(rta::platform::OutputEngine& engine,
                           std::function<rta::measure::GeneratorEnvironment()> environment);
    ~MainComponentGenerator() override;

    void attachTo(juce::Component& parent);
    /// One strip (the masthead's right side): GEN, - + , SIGNAL, FREQ, OUT and the readout.
    void layout(juce::Rectangle<int> strip);

    /// The handlers; public so the offscreen harness can call them.
    void genClicked();
    void stepClicked(int steps);
    void signalSelected(rta::measure::GeneratorSignal signal);
    void frequencySelected(int hz);
    /// Ticks or unticks one zero-based output. The last tick cannot be removed.
    void outputToggled(int channel);

    /// Called from MainComponent's 2 Hz timer; the 100 Hz one calls it too.
    void poll();
    /// Drops a running generator at once (input mode changed).
    void cancel(const std::string& reason);

    /// The generator holds, or is still releasing, the output.
    [[nodiscard]] bool active() const noexcept { return runner_.active(); }
    [[nodiscard]] const rta::measure::GeneratorSettings& settings() const noexcept { return runner_.settings(); }

    /// T13: the level and outputs a measurement uses right now, in the current
    /// measurement mode. `numOutputs` is the device's output count (0 = unknown).
    [[nodiscard]] rta::measure::MeasurementDrive measurementDrive(int numOutputs) const {
        return rta::measure::measurementDrive(measurementMode_, runner_.settings(), numOutputs);
    }

    /// One row per device output for the OUT menu: its label and whether it is ticked.
    [[nodiscard]] std::vector<std::pair<std::string, bool>> outputChoices() const;

    // --- test hooks, referenced by spelling from app/tests_juce ---------------
    [[nodiscard]] rta::measure::GeneratorRunner& runnerForTest() noexcept { return runner_; }
    [[nodiscard]] juce::String readoutForTest() const { return readout_.getText(); }
    [[nodiscard]] juce::String genButtonTextForTest() const { return genButton_.getButtonText(); }
    [[nodiscard]] const juce::Component& genButtonForTest() const noexcept { return genButton_; }
    [[nodiscard]] const juce::TextButton& minusButtonForTest() const noexcept { return minusButton_; }
    [[nodiscard]] const juce::TextButton& plusButtonForTest() const noexcept { return plusButton_; }
    [[nodiscard]] const juce::Component& readoutComponentForTest() const noexcept { return readout_; }
    [[nodiscard]] bool frequencyEnabledForTest() const noexcept { return frequencyCombo_.isEnabled(); }
    [[nodiscard]] int frequencyComboCountForTest() const noexcept { return frequencyCombo_.getNumItems(); }
    [[nodiscard]] bool fastPollRunningForTest() const noexcept { return isTimerRunning(); }

private:
    /// The level step buttons. The silkscreen face sets a hyphen and a plus as
    /// two specks at any size (PR #88 LOW), so the face is drawn as two bars; the
    /// text stays "-" / "+" as the control's accessible name.
    class StepButton final : public juce::TextButton {
    public:
        explicit StepButton(bool plus) : plus_(plus) {}
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

    private:
        bool plus_;
    };

    void timerCallback() override { poll(); }
    void refresh();
    void showOutputMenu();

    rta::measure::GeneratorRunner runner_;
    std::function<rta::measure::GeneratorEnvironment()> environment_;
    rta::measure::MeasurementLevelMode measurementMode_ = rta::measure::MeasurementLevelMode::FollowGen;
    juce::TextButton genButton_{"GEN"};
    StepButton minusButton_{false};
    StepButton plusButton_{true};
    juce::ComboBox signalCombo_;
    juce::ComboBox frequencyCombo_;
    juce::TextButton outButton_{"OUT"};
    juce::Label readout_;
};
