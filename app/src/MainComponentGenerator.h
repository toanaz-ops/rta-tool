// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. H7 task T3
// (docs/plans/2026-10-21-h7-generator-output-plan.md, wave A2): the rail's
// generator controller. GEN toggles the generator (it reads STOP while one is
// running), - and + step the level 1.0 dB, and a readout says what is
// playing. The state machine, the ceiling, the swap and every refusal live in
// rta::measure::GeneratorRunner / GeneratorModel (JUCE-free, proven with no
// device); this class only owns the widgets, forwards presses, and keeps a
// 100 Hz poll running while the runner is waiting on the audio callback.
//
// Wave A is the minimum safe operator path: pink noise on output 1. Signal,
// frequency and output choice (T6/T7) and the measurement-level mode (T13/T14)
// are later waves on top of the same runner.
//
// Nothing here persists: every launch is pink, -30.0 dBFS, out 1, stopped
// (owner decision D3).
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "measure/GeneratorRunner.h"
#include "rta/platform/OutputEngine.h"

#include <functional>
#include <string>

class MainComponentGenerator final : private juce::Timer {
public:
    /// `environment` is what only the composition root knows: SYNTHETIC or a
    /// running device, the output count, and which measurement (if any)
    /// holds the output. `engine` is borrowed and must outlive this object.
    MainComponentGenerator(rta::platform::OutputEngine& engine,
                           std::function<rta::measure::GeneratorEnvironment()> environment);
    ~MainComponentGenerator() override;

    void attachTo(juce::Component& parent);
    /// One strip (the masthead's right side): GEN, -, + and the readout.
    void layout(juce::Rectangle<int> strip);

    /// The three handlers; public so the offscreen harness can call them.
    void genClicked();
    void stepClicked(int steps);

    /// Called from MainComponent's 2 Hz timer; the 100 Hz one calls it too.
    void poll();
    /// Drops a running generator at once (input mode changed).
    void cancel(const std::string& reason);

    /// The generator holds, or is still releasing, the output.
    [[nodiscard]] bool active() const noexcept { return runner_.active(); }

    // --- test hooks, referenced by spelling from app/tests_juce ---------------
    [[nodiscard]] rta::measure::GeneratorRunner& runnerForTest() noexcept { return runner_; }
    [[nodiscard]] juce::String readoutForTest() const { return readout_.getText(); }
    [[nodiscard]] juce::String genButtonTextForTest() const { return genButton_.getButtonText(); }
    [[nodiscard]] const juce::Component& genButtonForTest() const noexcept { return genButton_; }
    [[nodiscard]] const juce::Component& readoutComponentForTest() const noexcept { return readout_; }
    [[nodiscard]] bool fastPollRunningForTest() const noexcept { return isTimerRunning(); }

private:
    void timerCallback() override { poll(); }
    void refresh();

    rta::measure::GeneratorRunner runner_;
    juce::TextButton genButton_{"GEN"};
    juce::TextButton minusButton_{"-"};
    juce::TextButton plusButton_{"+"};
    juce::Label readout_;
};
