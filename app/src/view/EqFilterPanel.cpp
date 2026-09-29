// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See EqFilterPanel.h.
#include "view/EqFilterPanel.h"

#include "rta/eq/FilterSpec.h"

#include <az_ui/az_ui.h>

#include <cstdio>

namespace rta::view {
namespace {

constexpr int kChipLabelHeight = 18;
constexpr int kChipButtonHeight = 22;
constexpr int kRowHeight = 22;
constexpr int kCaptionHeight = 18;

const char* typeWord(rta::eq::FilterType type) {
    switch (type) {
        case rta::eq::FilterType::LowShelf: return "lowshelf";
        case rta::eq::FilterType::HighShelf: return "highshelf";
        case rta::eq::FilterType::Peaking: break;
    }
    return "peaking";
}

}  // namespace

std::string eqFilterText(const rta::eq::FilterSpec& spec) {
    char text[96];
    std::snprintf(text, sizeof text, "%s %.0f Hz  Q %.2f  %+.1f dB", typeWord(spec.type), spec.fcHz, spec.q,
                  spec.gainDb);
    return text;
}

void EqFilterPanel::RowList::resized() {
    int y = 0;
    for (auto& toggle : toggles) {
        if (toggle != nullptr && toggle->isVisible()) {
            toggle->setBounds(0, y, getWidth(), kRowHeight);
            y += kRowHeight;
        }
    }
}

EqFilterPanel::EqFilterPanel() {
    for (std::size_t i = 0; i < chips_.size(); ++i) {
        auto& chip = chips_[i];
        chip.text.setJustificationType(juce::Justification::centredLeft);
        chip.accept.onClick = [this, i] { if (onAccept) onAccept(i); };
        chip.decline.onClick = [this, i] { if (onDecline) onDecline(i); };
        addChildComponent(chip.text);
        addChildComponent(chip.accept);
        addChildComponent(chip.decline);
    }
    listCaption_.setJustificationType(juce::Justification::centredLeft);
    listCaption_.setFont(az::ui::monoFont(az::ui::tableFontSize));
    addAndMakeVisible(listCaption_);
    viewport_.setViewedComponent(&rows_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
}

EqFilterPanel::~EqFilterPanel() { viewport_.setViewedComponent(nullptr, false); }

void EqFilterPanel::refresh(const rta::measure::EqPaneModel& model) {
    refreshing_ = true;
    const auto& candidates = model.chips();
    chipCount_ = std::min(candidates.size(), kMaxChips);
    for (std::size_t i = 0; i < chips_.size(); ++i) {
        const bool shown = i < chipCount_;
        if (shown) chips_[i].text.setText(juce::String(i + 1) + "  " + eqFilterText(candidates[i].spec), juce::dontSendNotification);
        chips_[i].text.setVisible(shown);
        chips_[i].accept.setVisible(shown);
        chips_[i].decline.setVisible(shown);
    }

    const auto filters = model.session().committed();
    rowCount_ = std::min(filters.size(), kMaxFilters);
    for (std::size_t i = 0; i < kMaxFilters; ++i) {
        auto& toggle = rows_.toggles[i];
        if (i >= rowCount_) {
            if (toggle != nullptr) toggle->setVisible(false);
            continue;
        }
        if (toggle == nullptr) {
            toggle = std::make_unique<juce::ToggleButton>();
            toggle->onClick = [this, i] {
                if (!refreshing_ && onApplied) onApplied(i, rows_.toggles[i]->getToggleState());
            };
            rows_.addChildComponent(*toggle);
        }
        toggle->setButtonText(eqFilterText(filters[i].spec));
        toggle->setToggleState(filters[i].applied, juce::dontSendNotification);
        toggle->setVisible(true);
    }
    listCaption_.setText(rowCount_ == 0 ? "COMMITTED: none"
                                        : "COMMITTED  (tick = already in the rig)",
                         juce::dontSendNotification);
    resized();
    refreshing_ = false;
}

void EqFilterPanel::resized() {
    auto area = getLocalBounds();
    for (std::size_t i = 0; i < chips_.size(); ++i) {
        if (!chips_[i].text.isVisible()) continue;
        // Two rows per chip: the text gets the full width (a 5-digit
        // frequency plus Q and gain does not fit beside two buttons), the
        // buttons share the row beneath it.
        chips_[i].text.setBounds(area.removeFromTop(kChipLabelHeight));
        auto buttons = area.removeFromTop(kChipButtonHeight);
        const int half = (buttons.getWidth() - 4) / 2;
        chips_[i].accept.setBounds(buttons.removeFromLeft(half));
        buttons.removeFromLeft(4);
        chips_[i].decline.setBounds(buttons);
        area.removeFromTop(4);
    }
    listCaption_.setBounds(area.removeFromTop(kCaptionHeight));
    viewport_.setBounds(area);
    rows_.setSize(std::max(1, area.getWidth() - viewport_.getScrollBarThickness()),
                  static_cast<int>(rowCount_) * kRowHeight);
    rows_.resized();  // setSize is a no-op when the size is unchanged, but the visible rows may not be
}

juce::Button& EqFilterPanel::acceptButtonForTest(std::size_t chip) { return chips_.at(chip).accept; }
juce::Button& EqFilterPanel::declineButtonForTest(std::size_t chip) { return chips_.at(chip).decline; }
juce::ToggleButton& EqFilterPanel::appliedToggleForTest(std::size_t row) { return *rows_.toggles.at(row); }
juce::String EqFilterPanel::chipTextForTest(std::size_t chip) const { return chips_.at(chip).text.getText(); }

}  // namespace rta::view
