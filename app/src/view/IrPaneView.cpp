// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See IrPaneView.h.
#include "view/IrPaneView.h"

#include <az_ui/az_ui.h>

#include <utility>

namespace rta::view {
namespace {

constexpr int kTimerHz = 10;
constexpr int kControlHeight = 28;
constexpr int kStoreWidth = 130;
constexpr int kModeWidth = 76;

juce::String fromUtf8(const std::string& text) { return juce::String(juce::CharPointer_UTF8(text.c_str())); }

}  // namespace

IrPaneView::IrPaneView(IrPaneBinding binding) : binding_(std::move(binding)) {
    irCombo_.setTextWhenNothingSelected("PICK A SWEEP IR");
    irCombo_.onChange = [this] { picked(); };

    storeButton_.getProperties().set(az::ui::hintProperty,
                                     "add the gated response to the trace library; TRANSFER draws it like any stored trace");
    storeButton_.onClick = [this] { storeClicked(); };

    readoutLabel_.setJustificationType(juce::Justification::centredLeft);
    readoutLabel_.setFont(az::ui::monoFont(az::ui::readoutFontSize));
    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    statusLabel_.setFont(az::ui::monoFont(az::ui::readoutFontSize));
    statusLabel_.setColour(juce::Label::textColourId, az::ui::dim);

    etcPlot_.onMarkersChanged = [this](const rta::measure::IrGateMarkers& m) { markersMoved(m); };
    etcPlot_.onMarkersSettled = [this] {
        if (binding_.settle) binding_.settle();
        refresh();
    };

    const char* const names[] = {"MAG", "PHASE", "EXCESS"};
    const char* const hints[] = {"the gated magnitude",
                                 "the gated phase beside its minimum phase (same magnitude)",
                                 "excess phase and excess group delay: what EQ cannot fix (delay, crossover, reflections)"};
    for (std::size_t i = 0; i < modeButtons_.size(); ++i) {
        modeButtons_[i] = std::make_unique<juce::TextButton>(names[i]);
        auto& b = *modeButtons_[i];
        b.setClickingTogglesState(false);
        b.getProperties().set(az::ui::hintProperty, hints[i]);
        b.onClick = [this, i] { lowerPlotPicked(static_cast<rta::measure::IrLowerPlot>(i)); };
        addAndMakeVisible(b);
    }

    for (juce::Component* c : std::initializer_list<juce::Component*>{&irCombo_, &storeButton_, &readoutLabel_,
                                                                       &statusLabel_, &etcPlot_, &gatedPlot_}) {
        addAndMakeVisible(*c);
    }
    refresh();
    startTimerHz(kTimerHz);  // a sweep finishing arrives from outside this pane
}

IrPaneView::~IrPaneView() { stopTimer(); }

void IrPaneView::setLibrary(const rta::trace::TraceLibrary*) {}

juce::StringArray IrPaneView::pickerTextsForTest() const {
    juce::StringArray out;
    for (int i = 0; i < irCombo_.getNumItems(); ++i) out.add(irCombo_.getItemText(i));
    return out;
}

void IrPaneView::paint(juce::Graphics& g) { g.fillAll(az::ui::background); }

void IrPaneView::resized() {
    auto area = getLocalBounds().reduced(az::ui::gap);
    auto top = area.removeFromTop(kControlHeight);
    storeButton_.setBounds(top.removeFromRight(kStoreWidth));
    top.removeFromRight(az::ui::gap);
    irCombo_.setBounds(top);
    area.removeFromTop(az::ui::gap);
    auto readoutRow = area.removeFromTop(kControlHeight);
    for (std::size_t i = modeButtons_.size(); i-- > 0;) modeButtons_[i]->setBounds(readoutRow.removeFromRight(kModeWidth));
    readoutLabel_.setBounds(readoutRow);
    statusLabel_.setBounds(area.removeFromBottom(kControlHeight));
    area.removeFromTop(az::ui::gap);
    const int etcHeight = area.getHeight() * 2 / 5;
    etcPlot_.setBounds(area.removeFromTop(etcHeight));
    area.removeFromTop(az::ui::gap);
    gatedPlot_.setBounds(area);
}

void IrPaneView::timerCallback() {
    refreshPicker();
    const auto revision = binding_.revision ? binding_.revision() : 0;
    if (revision != lastRevision_) refresh();
}

void IrPaneView::refreshPicker() {
    choices_ = binding_.choices ? binding_.choices() : std::vector<IrChoice>{};
    const std::string selected = binding_.selectedId ? binding_.selectedId() : std::string();
    juce::String key = fromUtf8(selected) + "|";
    for (const auto& c : choices_) key += fromUtf8(c.id) + ":" + fromUtf8(c.name) + ",";
    if (key != lastPickerKey_) {
        // Rebuilt only when something changed, never under the operator's open popup.
        lastPickerKey_ = key;
        irCombo_.clear(juce::dontSendNotification);
        for (std::size_t i = 0; i < choices_.size(); ++i) {
            irCombo_.addItem(fromUtf8(choices_[i].name), static_cast<int>(i) + 1);
        }
    }
    int id = 0;
    for (std::size_t i = 0; i < choices_.size() && !selected.empty(); ++i) {
        if (choices_[i].id == selected) id = static_cast<int>(i) + 1;
    }
    irCombo_.setSelectedId(id, juce::dontSendNotification);
}

void IrPaneView::picked() {
    const int id = irCombo_.getSelectedId();
    if (id < 1 || static_cast<std::size_t>(id) > choices_.size() || !binding_.select) return;
    binding_.select(choices_[static_cast<std::size_t>(id) - 1].id);
    statusLabel_.setText({}, juce::dontSendNotification);
    refresh();
}

void IrPaneView::markersMoved(const rta::measure::IrGateMarkers& markers) {
    if (binding_.setMarkers) binding_.setMarkers(markers);
    statusLabel_.setText({}, juce::dontSendNotification);
    refresh();
}

void IrPaneView::storeClicked() {
    if (!binding_.storeGated) return;
    statusLabel_.setText(fromUtf8(binding_.storeGated()), juce::dontSendNotification);
}

void IrPaneView::lowerPlotPicked(rta::measure::IrLowerPlot mode) {
    if (binding_.setLowerPlot) binding_.setLowerPlot(mode);
    refresh();
}

void IrPaneView::refresh() {
    lastRevision_ = binding_.revision ? binding_.revision() : 0;
    refreshPicker();
    const auto etc = binding_.etc ? binding_.etc() : nullptr;
    const auto analysis = binding_.analysis ? binding_.analysis() : nullptr;
    const auto markers = binding_.markers ? binding_.markers() : rta::measure::IrGateMarkers{};
    const bool picked = etc != nullptr && analysis != nullptr;

    etcPlot_.setData(etc, markers, analysis != nullptr && analysis->ok);
    std::string empty = "PRESS SWEEP TO MAKE AN IR, THEN PICK IT (IRs are kept in memory, not in a saved session)";
    if (picked) empty = analysis->ok ? std::string() : analysis->readout;
    using rta::measure::IrLowerPlot;
    const IrLowerPlot mode = binding_.lowerPlot ? binding_.lowerPlot() : IrLowerPlot::Magnitude;
    const auto excess = binding_.excess ? binding_.excess() : nullptr;
    // PHASE / EXCESS need the decomposition of a SETTLED gate: until it exists the plot says
    // what it is waiting for, and an invalid one says why (the kernel's own sentence).
    if (picked && analysis->ok && mode != IrLowerPlot::Magnitude) {
        if (excess == nullptr) {
            empty = binding_.excessPending && binding_.excessPending() ? "computing excess phase..."
                                                                       : "release the marker to compute excess phase";
        } else if (!excess->valid) {
            empty = excess->refusal;
        }
    }
    gatedPlot_.setData(analysis, excess, mode, empty);
    for (std::size_t i = 0; i < modeButtons_.size(); ++i) {
        modeButtons_[i]->setToggleState(static_cast<std::size_t>(mode) == i, juce::dontSendNotification);
    }

    std::string readout = picked ? analysis->readout : std::string("GATE -- no IR picked");
    if (picked && analysis->ok && mode != IrLowerPlot::Magnitude && excess != nullptr && excess->valid) {
        readout += " · " + excess->tau0Text();
    }
    readoutLabel_.setText(fromUtf8(readout), juce::dontSendNotification);
    readoutLabel_.setColour(juce::Label::textColourId,
                            picked && !analysis->ok ? juce::Colour(az::ui::warn) : juce::Colour(az::ui::text));
    storeButton_.setEnabled(picked && analysis->ok);
    repaint();
}

}  // namespace rta::view
