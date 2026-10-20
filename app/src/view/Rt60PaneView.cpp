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
constexpr int kButtonWidth = 120;
constexpr int kComboWidth = 110;
constexpr int kHeaderHeight = 38;
constexpr int kStatusHeight = 20;
constexpr int kRowHeight = 26;
constexpr int kAdviceHeight = 40;
constexpr int kFootnoteHeight = 20;

// Column widths of the painted table; the note takes whatever is left.
constexpr int kHzWidth = 64;
constexpr int kFigureWidth = 96;
constexpr int kRangeWidth = 100;

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
                                                                       &sweepButton_, &headerLabel_, &statusLabel_,
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
    rows_.clear();
    tableLines_.clear();
    wholeRefusal_.clear();
    juce::String header, advice, footnote;
    if (shown_ != nullptr) {
        const std::string name = binding_.selectedName ? binding_.selectedName() : std::string();
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
    repaint();
}

void Rt60PaneView::paint(juce::Graphics& g) {
    g.fillAll(az::ui::background);
    lastPaintDrewTable_ = false;
    if (shown_ == nullptr || (!shown_->analysed() && wholeRefusal_.empty())) {
        g.setColour(emptyStateText);
        g.setFont(az::ui::legendFont(az::ui::captionFontSize, true, az::ui::trackingCaption));
        g.drawFittedText("PICK A SWEEP IR, OR PRESS SWEEP", tableArea_.reduced(az::ui::gap * 2),
                         juce::Justification::centred, 2);
        return;
    }
    if (!shown_->analysed()) {
        // The one refusal the pane has to show, whole -- not a table of dashes.
        g.setColour(az::ui::warn);
        g.setFont(az::ui::monoFont(az::ui::tableFontSize));
        g.drawFittedText(fromUtf8(wholeRefusal_), tableArea_.reduced(az::ui::gap * 2), juce::Justification::centredLeft,
                         4);
        return;
    }

    // Column heading, then one row per band. Numbers right-aligned in mono so the
    // decimal points line up; the note takes the remaining width.
    auto area = tableArea_;
    const auto columns = [](juce::Rectangle<int> row) {
        struct Cells {
            juce::Rectangle<int> hz, t30, t20, edt, range, note;
        } c;
        c.hz = row.removeFromLeft(kHzWidth);
        c.t30 = row.removeFromLeft(kFigureWidth);
        c.t20 = row.removeFromLeft(kFigureWidth);
        c.edt = row.removeFromLeft(kFigureWidth);
        c.range = row.removeFromLeft(kRangeWidth);
        row.removeFromLeft(az::ui::gap * 2);
        c.note = row;
        return c;
    };
    const auto numberCell = [&g](juce::Rectangle<int> cell, const juce::String& text, juce::Colour colour) {
        g.setColour(colour);
        g.drawText(text, cell.withTrimmedRight(az::ui::gap), juce::Justification::centredRight, true);
    };

    {
        const auto head = columns(area.removeFromTop(kRowHeight));
        g.setColour(az::ui::dim);
        g.setFont(az::ui::legendFont(az::ui::columnFontSize, true, az::ui::trackingColumn));
        for (const auto& [cell, text] : {std::pair{head.hz, "Hz"}, std::pair{head.t30, "T30"}, std::pair{head.t20, "T20"},
                                         std::pair{head.edt, "EDT*"}, std::pair{head.range, "RANGE"}}) {
            g.drawText(text, cell.withTrimmedRight(az::ui::gap), juce::Justification::centredRight, true);
        }
        g.drawText("NOTE", head.note, juce::Justification::centredLeft, true);
        g.setColour(az::ui::border);
        g.drawHorizontalLine(area.getY() - 1, static_cast<float>(tableArea_.getX()),
                             static_cast<float>(tableArea_.getRight()));
    }

    g.setFont(az::ui::monoFont(az::ui::tableFontSize));
    for (const auto& row : rows_) {
        const auto cells = columns(area.removeFromTop(kRowHeight));
        const auto figureColour = [](const std::string& cell, juce::Colour normal) {
            return cell == "--" ? az::ui::faded : normal;
        };
        numberCell(cells.hz, fromUtf8(row.hz), az::ui::text);
        numberCell(cells.t30, fromUtf8(row.t30), figureColour(row.t30, az::ui::accent));
        numberCell(cells.t20, fromUtf8(row.t20), figureColour(row.t20, az::ui::text));
        numberCell(cells.edt, fromUtf8(row.edt), figureColour(row.edt, az::ui::dim));
        numberCell(cells.range, fromUtf8(row.range), figureColour(row.range, az::ui::dim));
        g.setColour(az::ui::warn);
        g.drawFittedText(fromUtf8(row.note), cells.note, juce::Justification::centredLeft, 2);
        g.setColour(az::ui::border.withAlpha(0.5f));
        g.drawHorizontalLine(cells.hz.getBottom(), static_cast<float>(tableArea_.getX()),
                             static_cast<float>(tableArea_.getRight()));
    }
    lastPaintDrewTable_ = true;
}

void Rt60PaneView::resized() {
    auto area = getLocalBounds().reduced(az::ui::gap);
    auto top = area.removeFromTop(az::ui::fieldHeight);
    sweepButton_.setBounds(top.removeFromRight(kButtonWidth));
    top.removeFromRight(az::ui::gap);
    tailCombo_.setBounds(top.removeFromRight(kComboWidth));
    top.removeFromRight(az::ui::gap);
    outputCombo_.setBounds(top.removeFromRight(kComboWidth));
    top.removeFromRight(az::ui::gap);
    irCombo_.setBounds(top);
    area.removeFromTop(az::ui::gap);

    headerLabel_.setBounds(area.removeFromTop(kHeaderHeight));
    statusLabel_.setBounds(area.removeFromTop(kStatusHeight));
    area.removeFromTop(az::ui::gap);
    footnoteLabel_.setBounds(area.removeFromBottom(kFootnoteHeight));
    adviceLabel_.setBounds(area.removeFromBottom(kAdviceHeight));
    tableArea_ = area;
}

}  // namespace rta::view
