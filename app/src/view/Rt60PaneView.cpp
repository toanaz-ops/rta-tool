// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See Rt60PaneView.h.
#include "view/Rt60PaneView.h"

#include "measure/Rt60Readout.h"
#include "trace/IrStore.h"
#include "view/MeasureColours.h"

#include <az_ui/az_ui.h>

#include <algorithm>
#include <utility>

namespace rta::view {
namespace {

constexpr int kTimerHz = 10;
constexpr int kMaxListedOutputs = 16;


juce::String fromUtf8(const std::string& text) { return juce::String(juce::CharPointer_UTF8(text.c_str())); }

// The label a picker entry carries: the trace's name, then whether its IR is
// still in memory or why it is not (plan decision 9: never hidden).
juce::String choiceLabel(const Rt60Choice& choice) {
    const juce::String tag = choice.hasIr
        ? juce::String("IR in memory")
        : fromUtf8(rta::measure::rt60NoIrText(rta::trace::IrStore::kCapacity));
    return fromUtf8(choice.name) + "  [" + tag + "]";
}

}  // namespace

Rt60PaneView::Rt60PaneView(Rt60PaneBinding binding) : binding_(std::move(binding)) {
    irCombo_.setTextWhenNothingSelected("PICK A SWEEP IR");
    irCombo_.onChange = [this] { picked(); };
    for (std::size_t i = 0; i < rta::measure::kRt60TailChoicesSeconds.size(); ++i) {
        tailCombo_.addItem("TAIL " + juce::String(static_cast<int>(rta::measure::kRt60TailChoicesSeconds[i])) + " s",
                           static_cast<int>(i) + 1);
    }
    // The tail restores from the settings the controller owns, so a rebuilt pane
    // shows what the next press will really use.
    int tailId = 1;
    if (binding_.settings != nullptr) {
        for (std::size_t i = 0; i < rta::measure::kRt60TailChoicesSeconds.size(); ++i) {
            if (rta::measure::kRt60TailChoicesSeconds[i] == binding_.settings->tailSeconds) {
                tailId = static_cast<int>(i) + 1;
            }
        }
    }
    tailCombo_.setSelectedId(tailId, juce::dontSendNotification);
    tailCombo_.onChange = [this] {
        const int id = tailCombo_.getSelectedId();
        if (binding_.settings != nullptr && id >= 1 &&
            static_cast<std::size_t>(id) <= rta::measure::kRt60TailChoicesSeconds.size()) {
            binding_.settings->tailSeconds = rta::measure::kRt60TailChoicesSeconds[static_cast<std::size_t>(id) - 1];
        }
    };
    outputCombo_.onChange = [this] {
        const int id = outputCombo_.getSelectedId();
        if (binding_.settings != nullptr && id >= 1) binding_.settings->outputChannel = id - 1;
    };

    sweepButton_.getProperties().set(az::ui::hintProperty,
                                     "log sweep on the chosen output with the chosen tail, then analyse it");
    sweepButton_.onClick = [this] {
        if (!binding_.sweep || binding_.settings == nullptr) return;
        binding_.sweep(binding_.settings->outputChannel, binding_.settings->tailSeconds);
        refresh();
    };

    bandSetButton_.getProperties().set(az::ui::hintProperty,
                                       "switch the table between octave bands and third-octave bands (50 Hz to 10 kHz)");
    bandSetButton_.onClick = [this] {
        if (binding_.settings == nullptr || !binding_.setBandSet) return;
        const bool toThird = binding_.settings->bandSet == rta::measure::Rt60BandSet::Octave;
        binding_.setBandSet(toThird ? rta::measure::Rt60BandSet::Third : rta::measure::Rt60BandSet::Octave);
        refresh();
    };
    ensembleButton_.getProperties().set(
        az::ui::hintProperty, "tick up to 8 swept captures: the table shows their median and, from 3, the spread");
    ensembleButton_.onClick = [this] {
        std::vector<std::string> ids;
        auto menu = buildEnsembleMenu(&ids);
        if (menu.getNumItems() == 0) return;
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&ensembleButton_),
                           [this, safe = juce::Component::SafePointer<Rt60PaneView>(this)](int itemId) {
                               if (safe != nullptr && itemId > 0) chooseEnsembleEntry(itemId - 1);
                           });
    };

    headerLabel_.setJustificationType(juce::Justification::topLeft);
    headerLabel_.setFont(az::ui::monoFont(az::ui::readoutFontSize));
    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    statusLabel_.setFont(az::ui::monoFont(az::ui::readoutFontSize));
    statusLabel_.setColour(juce::Label::textColourId, az::ui::dim);
    adviceLabel_.setJustificationType(juce::Justification::topLeft);
    adviceLabel_.setFont(az::ui::monoFont(az::ui::readoutFontSize));
    adviceLabel_.setColour(juce::Label::textColourId, az::ui::warn);
    footnoteLabel_.setJustificationType(juce::Justification::centredLeft);
    footnoteLabel_.setFont(az::ui::monoFont(az::ui::readoutFontSize));
    footnoteLabel_.setColour(juce::Label::textColourId, az::ui::faded);

    for (juce::Component* c : std::initializer_list<juce::Component*>{&irCombo_, &outputCombo_, &tailCombo_,
                                                                       &sweepButton_, &ensembleButton_, &bandSetButton_,
                                                                       &chart_, &headerLabel_, &statusLabel_,
                                                                       &adviceLabel_, &footnoteLabel_}) {
        addAndMakeVisible(*c);
    }
    refresh();
    startTimerHz(kTimerHz);  // a sweep finishing arrives from outside this pane
}

Rt60PaneView::~Rt60PaneView() { stopTimer(); }

void Rt60PaneView::setLibrary(const rta::trace::TraceLibrary*) {}

juce::StringArray Rt60PaneView::pickerTextsForTest() const {
    juce::StringArray out;
    for (int i = 0; i < irCombo_.getNumItems(); ++i) out.add(irCombo_.getItemText(i));
    return out;
}

void Rt60PaneView::timerCallback() {
    // The sweep readout and the picker follow a sweep that is not a revision of
    // the shown result; the table follows the revision.
    refreshPicker();
    refreshOutputCombo();
    const bool busy = binding_.sweepBusy && binding_.sweepBusy();
    sweepButton_.setEnabled(!busy && binding_.sweep && binding_.outputChannels && binding_.outputChannels() > 0);
    const juce::String status = binding_.sweepReadout ? fromUtf8(binding_.sweepReadout()) : juce::String();
    if (status != statusLabel_.getText()) statusLabel_.setText(status, juce::dontSendNotification);
    const auto revision = binding_.revision ? binding_.revision() : 0;
    if (revision != lastRevision_) refresh();
}

void Rt60PaneView::refreshPicker() {
    choices_ = binding_.choices ? binding_.choices() : std::vector<Rt60Choice>{};
    const std::string selected = binding_.selectedId ? binding_.selectedId() : std::string();
    juce::String key = fromUtf8(selected) + "|";
    for (const auto& c : choices_) key += fromUtf8(c.id) + (c.hasIr ? "+" : "-") + fromUtf8(c.name) + ",";
    if (key != lastPickerKey_) {
        // Rebuilt only when something changed, never under the operator's open popup.
        lastPickerKey_ = key;
        irCombo_.clear(juce::dontSendNotification);
        for (std::size_t i = 0; i < choices_.size(); ++i) {
            irCombo_.addItem(choiceLabel(choices_[i]), static_cast<int>(i) + 1);
        }
    }
    int id = 0;
    for (std::size_t i = 0; i < choices_.size() && !selected.empty(); ++i) {
        if (choices_[i].id == selected) id = static_cast<int>(i) + 1;
    }
    irCombo_.setSelectedId(id, juce::dontSendNotification);
}

void Rt60PaneView::refreshOutputCombo() {
    const int count = binding_.outputChannels ? std::min(binding_.outputChannels(), kMaxListedOutputs) : 0;
    if (count == lastOutputCount_) return;
    lastOutputCount_ = count;
    outputCombo_.clear(juce::dontSendNotification);
    for (int ch = 0; ch < count; ++ch) outputCombo_.addItem("OUT " + juce::String(ch + 1), ch + 1);
    const int selected = binding_.settings != nullptr ? binding_.settings->outputChannel : 0;
    outputCombo_.setSelectedId(selected < count ? selected + 1 : 0, juce::dontSendNotification);
}

void Rt60PaneView::picked() {
    const int id = irCombo_.getSelectedId();
    if (id < 1 || static_cast<std::size_t>(id) > choices_.size() || !binding_.select) return;
    binding_.select(choices_[static_cast<std::size_t>(id) - 1].id);
    refresh();
}

void Rt60PaneView::refresh() {
    lastRevision_ = binding_.revision ? binding_.revision() : 0;
    refreshPicker();
    refreshOutputCombo();

    shown_ = binding_.result ? binding_.result() : nullptr;
    ensembleShown_ = binding_.ensembleResult ? binding_.ensembleResult() : nullptr;
    analysingShown_ = binding_.analysing && binding_.analysing();
    rows_.clear();
    tableLines_.clear();
    wholeRefusal_.clear();
    columnsAreEnsemble_ = false;
    juce::String header, advice, footnote;
    const std::string name = binding_.selectedName ? binding_.selectedName() : std::string();
    if (analysingShown_) {
        // The worker holds the job: say so rather than show a table for the other band set.
        header = "analysing in the background...\n";
    } else if (ensembleShown_ != nullptr) {
        // Two or more captures ticked: the table is THEIR median (and, from three,
        // spread). The plotted curve below is still the picked trace's.
        const auto names = binding_.ensembleNames ? binding_.ensembleNames() : std::vector<std::string>{};
        for (const auto& line : rta::measure::rt60EnsembleHeaderLines(*ensembleShown_, names)) {
            header += fromUtf8(line) + "\n";
        }
        if (ensembleShown_->analysed()) {
            columnsAreEnsemble_ = true;
            for (const auto& band : ensembleShown_->bands) {
                rows_.push_back(rta::measure::rt60EnsembleRow(band, *ensembleShown_));
            }
            tableLines_ = rta::measure::rt60EnsembleTableLines(*ensembleShown_);
            footnote = fromUtf8(rta::measure::rt60Footnote());
        } else {
            wholeRefusal_ = rta::measure::rt60EnsembleRefusalText(*ensembleShown_);
        }
    } else if (shown_ != nullptr) {
        const bool inMemory = binding_.selectedIrInMemory ? binding_.selectedIrInMemory() : true;
        for (const auto& line : rta::measure::rt60HeaderLines(*shown_, name, inMemory)) header += fromUtf8(line) + "\n";
        if (shown_->analysed()) {
            for (const auto& band : shown_->bands) rows_.push_back(rta::measure::rt60Row(band, *shown_));
            tableLines_ = rta::measure::rt60TableLines(*shown_);
            advice = fromUtf8(rta::measure::rt60Advice(*shown_));
            footnote = fromUtf8(rta::measure::rt60Footnote());
        } else {
            wholeRefusal_ = rta::measure::rt60WholeRefusalText(*shown_, rta::trace::IrStore::kCapacity);
        }
    }
    headerLabel_.setText(header, juce::dontSendNotification);
    adviceLabel_.setText(advice, juce::dontSendNotification);
    footnoteLabel_.setText(footnote, juce::dontSendNotification);
    refreshButtons();
    layoutBody();
    refreshChart();
    repaint();
}

void Rt60PaneView::refreshButtons() {
    const bool third = binding_.settings != nullptr && binding_.settings->bandSet == rta::measure::Rt60BandSet::Third;
    bandSetButton_.setButtonText(third ? "THIRD" : "OCTAVE");
    const auto ids = binding_.ensembleIds ? binding_.ensembleIds() : std::vector<std::string>{};
    ensembleButton_.setButtonText(ids.empty() ? juce::String("ENSEMBLE") : "ENSEMBLE (" + juce::String(static_cast<int>(ids.size())) + ")");
}

}  // namespace rta::view
