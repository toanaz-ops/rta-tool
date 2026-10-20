// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view.
//
// ALIGN-UI wave B, task T11 (docs/plans/2026-10-20-align-ui-lane-plan.md,
// decisions 9 and 13): the IR half of the ALIGN pane. SWEEP MAIN and SWEEP SUB
// run H4's sweep on the output the operator named for that source; a picker per
// source lists the library's SWEEP traces whose IR is still in memory
// (`IrStore::find`) and binds one to that source. The just-swept trace binds
// itself. Bound IRs go to the wizard at MEASURE, keyed by SOURCE, each with its
// arrival -- which is what lets the cycle integer come from a measurement
// instead of from the operator's typed offset.
//
// Widgets and text only. What a click does, and which IRs exist, arrive through
// `AlignIrHooks` (MainComponentAlign supplies them); what is bound lives in the
// AlignRunner, so a pane rebuilt by a selector click loses nothing.
#pragma once

#include "measure/AlignRunner.h"
#include "measure/SweepIrOutcome.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rta::view {

/// One library SWEEP trace whose IR is still in memory.
struct AlignIrChoice {
    std::string id;     ///< the library trace id (the IrStore key)
    std::string label;  ///< the trace's name, as the library shows it
};

struct AlignIrHooks {
    /// SWEEP MAIN / SWEEP SUB: sweep on the output named for that source.
    std::function<void(rta::measure::Source)> sweepOnSide;
    /// Library SWEEP traces with a live IR, newest first.
    std::function<std::vector<AlignIrChoice>()> choices;
    /// The IR for a trace id; null when it was never kept or has been evicted.
    std::function<std::shared_ptr<const rta::measure::SweepIr>(const std::string&)> lookup;
    /// The sweep's own readout line ("sweep: playing...", "IR +96 samples ...").
    std::function<std::string()> sweepReadout;
    std::function<bool()> sweepBusy;
};

class AlignIrPanel final : public juce::Component {
public:
    AlignIrPanel(rta::measure::AlignRunner& runner, AlignIrHooks hooks);

    /// Enable states and the picker lists, from the runner and the hooks. The
    /// pane calls it on every tick: a sweep finishing is not a runner event.
    void refresh();
    /// The text the pane shows beside its verdict: what is bound, the sweep's
    /// readout, the polarity signals and how long the fit took.
    [[nodiscard]] juce::String infoText() const;
    void resized() override;

    [[nodiscard]] juce::Button& sweepMainButtonForTest() noexcept { return sweepMain_; }
    [[nodiscard]] juce::Button& sweepSubButtonForTest() noexcept { return sweepSub_; }
    [[nodiscard]] juce::ComboBox& mainPickerForTest() noexcept { return mainPicker_; }
    [[nodiscard]] juce::ComboBox& subPickerForTest() noexcept { return subPicker_; }

private:
    void fillPicker(juce::ComboBox& picker, rta::measure::Source side,
                    const std::vector<AlignIrChoice>& choices);
    void picked(juce::ComboBox& picker, rta::measure::Source side);
    [[nodiscard]] juce::String boundLine(rta::measure::Source side,
                                         const std::vector<AlignIrChoice>& choices) const;

    rta::measure::AlignRunner& runner_;
    AlignIrHooks hooks_;
    juce::TextButton sweepMain_{ "SWEEP MAIN" }, sweepSub_{ "SWEEP SUB" };
    juce::ComboBox mainPicker_, subPicker_;
    /// What each picker was last filled from, so a tick that changed nothing
    /// does not rebuild a list under the operator's open popup.
    juce::String mainKey_, subKey_;
    std::vector<AlignIrChoice> mainItems_, subItems_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlignIrPanel)
};

}  // namespace rta::view
