// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See AlignIrPanel.h.
#include "view/AlignIrPanel.h"

#include <az_ui/az_ui.h>

#include <algorithm>
#include <cstdio>

namespace rta::view {
namespace {

using rta::measure::Source;

juce::String fromUtf8(const std::string& text) { return juce::String(juce::CharPointer_UTF8(text.c_str())); }

juce::String keyOf(const std::vector<AlignIrChoice>& items, const std::string& bound) {
    juce::String key = fromUtf8(bound) + "|";
    for (const auto& item : items) key += fromUtf8(item.id) + ",";
    return key;
}

}  // namespace

AlignIrPanel::AlignIrPanel(rta::measure::AlignRunner& runner, AlignIrHooks hooks)
    : runner_(runner), hooks_(std::move(hooks)) {
    sweepMain_.getProperties().set(az::ui::hintProperty,
                                   "log sweep on the MAIN output only; the IR binds itself to MAIN");
    sweepSub_.getProperties().set(az::ui::hintProperty,
                                  "log sweep on the SUB output only; the IR binds itself to SUB");
    sweepMain_.onClick = [this] {
        if (hooks_.sweepOnSide) hooks_.sweepOnSide(Source::Main);
    };
    sweepSub_.onClick = [this] {
        if (hooks_.sweepOnSide) hooks_.sweepOnSide(Source::Sub);
    };
    mainPicker_.setTextWhenNothingSelected("MAIN IR: NONE");
    subPicker_.setTextWhenNothingSelected("SUB IR: NONE");
    mainPicker_.onChange = [this] { picked(mainPicker_, Source::Main); };
    subPicker_.onChange = [this] { picked(subPicker_, Source::Sub); };
    for (juce::Component* c : std::initializer_list<juce::Component*>{ &sweepMain_, &mainPicker_, &sweepSub_,
                                                                       &subPicker_ }) {
        addAndMakeVisible(*c);
    }
}

void AlignIrPanel::fillPicker(juce::ComboBox& picker, Source side, const std::vector<AlignIrChoice>& choices) {
    auto& items = (side == Source::Main) ? mainItems_ : subItems_;
    auto& key = (side == Source::Main) ? mainKey_ : subKey_;
    const std::string& bound = runner_.boundIrId(side);
    const juce::String next = keyOf(choices, bound);
    if (next != key) {
        key = next;
        items = choices;
        // A bound IR the store has since evicted is still in use by the runner,
        // so the picker keeps showing it rather than claiming nothing is bound.
        if (!bound.empty() && std::none_of(items.begin(), items.end(), [&](const AlignIrChoice& c) { return c.id == bound; })) {
            items.push_back({ bound, bound + " (kept, no longer listed)" });
        }
        picker.clear(juce::dontSendNotification);
        picker.addItem("NO IR (TYPE THE OFFSET)", 1);
        for (std::size_t i = 0; i < items.size(); ++i) {
            picker.addItem(fromUtf8(items[i].label), static_cast<int>(i) + 2);
        }
    }
    int selected = 0;
    for (std::size_t i = 0; i < items.size() && !bound.empty(); ++i) {
        if (items[i].id == bound) selected = static_cast<int>(i) + 2;
    }
    picker.setSelectedId(selected, juce::dontSendNotification);
}

void AlignIrPanel::picked(juce::ComboBox& picker, Source side) {
    const auto& items = (side == Source::Main) ? mainItems_ : subItems_;
    const int id = picker.getSelectedId();
    if (id < 2 || static_cast<std::size_t>(id) - 2 >= items.size()) {
        runner_.bindIr(side, {}, nullptr);
        return;
    }
    const AlignIrChoice& choice = items[static_cast<std::size_t>(id) - 2];
    // Looked up again at pick time: the choice list is a moment old, and an
    // evicted IR must unbind rather than bind a stale pointer.
    auto ir = hooks_.lookup ? hooks_.lookup(choice.id) : nullptr;
    if (ir == nullptr && runner_.boundIrId(side) == choice.id) return;  // the "kept" entry: nothing to change
    runner_.bindIr(side, choice.id, std::move(ir));
}

void AlignIrPanel::refresh() {
    const auto choices = hooks_.choices ? hooks_.choices() : std::vector<AlignIrChoice>{};
    fillPicker(mainPicker_, Source::Main, choices);
    fillPicker(subPicker_, Source::Sub, choices);
    // The same predicate both ways: SWEEP refuses while ALIGN runs (outputBusy
    // includes it) and ALIGN refuses while SWEEP runs. A disabled button is the
    // pane saying so before the click, not a second source of truth.
    const bool free = !runner_.busy() && !(hooks_.sweepBusy && hooks_.sweepBusy());
    const bool outputsValid = runner_.outputChannels() > 0;
    sweepMain_.setEnabled(free && outputsValid);
    sweepSub_.setEnabled(free && outputsValid);
    mainPicker_.setEnabled(!runner_.busy());
    subPicker_.setEnabled(!runner_.busy());
}

juce::String AlignIrPanel::boundLine(Source side, const std::vector<AlignIrChoice>& choices) const {
    const char* name = side == Source::Main ? "MAIN" : "SUB ";
    const auto ir = runner_.boundIr(side);
    if (ir == nullptr) return juce::String("IR ") + juce::String(name) + ": none bound";
    juce::String label = fromUtf8(runner_.boundIrId(side));
    for (const auto& choice : choices) {
        if (choice.id == runner_.boundIrId(side)) label = fromUtf8(choice.label);
    }
    char arrival[96];
    std::snprintf(arrival, sizeof arrival, " arrival %+ld samples (%+.2f ms)", ir->arrivalSamples,
                  1000.0 * static_cast<double>(ir->arrivalSamples) / ir->sampleRate);
    return juce::String("IR ") + juce::String(name) + ": " + label + juce::String(arrival);
}

juce::String AlignIrPanel::infoText() const {
    const auto choices = hooks_.choices ? hooks_.choices() : std::vector<AlignIrChoice>{};
    juce::String text = boundLine(Source::Main, choices) + "\n" + boundLine(Source::Sub, choices) + "\n";
    if (hooks_.sweepReadout) text += fromUtf8(hooks_.sweepReadout()) + "\n";
    for (const auto& line : runner_.polaritySignalLines()) text += fromUtf8(line) + "\n";
    if (runner_.lastComputeMs() > 0.0) {
        char timing[64];
        std::snprintf(timing, sizeof timing, "fit + polarity: %.1f ms on this thread", runner_.lastComputeMs());
        text += timing;
    }
    return text;
}

void AlignIrPanel::resized() {
    auto area = getLocalBounds();
    const int gap = az::ui::gap;
    const int buttonWidth = 120;
    const int pickerWidth = (area.getWidth() - 2 * buttonWidth - 3 * gap) / 2;
    sweepMain_.setBounds(area.removeFromLeft(buttonWidth));
    area.removeFromLeft(gap);
    mainPicker_.setBounds(area.removeFromLeft(pickerWidth));
    area.removeFromLeft(gap);
    sweepSub_.setBounds(area.removeFromLeft(buttonWidth));
    area.removeFromLeft(gap);
    subPicker_.setBounds(area);
}

}  // namespace rta::view
