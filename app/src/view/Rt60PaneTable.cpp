// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. The table, the layout, the row click, the
// chart's feed and the ensemble menu of Rt60PaneView (see Rt60PaneView.h); split
// from Rt60PaneView.cpp, which keeps the constructor, the timer and the refresh,
// to hold both files under the project's 400-line cap.
#include "view/Rt60PaneView.h"

#include "view/MeasureColours.h"

#include <az_ui/az_ui.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace rta::view {
namespace {

constexpr int kButtonWidth = 120;
constexpr int kComboWidth = 110;
constexpr int kBandSetWidth = 90;
constexpr int kEnsembleWidth = 130;
constexpr int kHeaderHeight = 38;
constexpr int kStatusHeight = 20;
constexpr int kRowHeight = 26;
constexpr int kMinRowHeight = 15;     // a third-octave table's 24 rows must leave the chart room
constexpr int kChartMinHeight = 150;
constexpr int kAdviceHeight = 40;
constexpr int kFootnoteHeight = 20;

// Column widths of the painted table; the note takes whatever is left. The
// ensemble's figure columns are wider: "0.80 s  IQR 4%" does not fit in 96 px.
constexpr int kHzWidth = 64;
constexpr int kFigureWidth = 96;
constexpr int kEnsembleFigureWidth = 150;
constexpr int kRangeWidth = 100;

juce::String fromUtf8(const std::string& text) { return juce::String(juce::CharPointer_UTF8(text.c_str())); }

}  // namespace

std::size_t Rt60PaneView::bandCount() const {
    if (!rows_.empty()) return rows_.size();
    const bool third = binding_.settings != nullptr && binding_.settings->bandSet == rta::measure::Rt60BandSet::Third;
    return rta::measure::rt60Bands(third ? rta::measure::Rt60BandSet::Third : rta::measure::Rt60BandSet::Octave).size();
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
    bandSetButton_.setBounds(top.removeFromRight(kBandSetWidth));
    top.removeFromRight(az::ui::gap);
    ensembleButton_.setBounds(top.removeFromRight(kEnsembleWidth));
    top.removeFromRight(az::ui::gap);
    irCombo_.setBounds(top);
    area.removeFromTop(az::ui::gap);
    rowsArea_ = area;
    layoutBody();
}

// Everything under the controls. The table takes the height its rows need (rows
// shrink toward kMinRowHeight for a third-octave set) and the chart the rest, so
// the curve is never squeezed out by the table.
void Rt60PaneView::layoutBody() {
    auto area = rowsArea_;
    headerLabel_.setBounds(area.removeFromTop(kHeaderHeight));
    statusLabel_.setBounds(area.removeFromTop(kStatusHeight));
    area.removeFromTop(az::ui::gap);
    footnoteLabel_.setBounds(area.removeFromBottom(kFootnoteHeight));
    adviceLabel_.setBounds(area.removeFromBottom(kAdviceHeight));

    const int lines = static_cast<int>(bandCount()) + 1;  // + the column heading
    rowHeight_ = std::clamp((area.getHeight() - kChartMinHeight) / lines, kMinRowHeight, kRowHeight);
    tableArea_ = area.removeFromTop(std::min(lines * rowHeight_, area.getHeight()));
    area.removeFromTop(az::ui::gap);
    chart_.setBounds(area);
}

void Rt60PaneView::mouseDown(const juce::MouseEvent& event) {
    if (!lastPaintDrewTable_ || !tableArea_.contains(event.getPosition())) return;
    const int row = (event.y - tableArea_.getY()) / rowHeight_ - 1;  // the heading is row -1
    if (row >= 0) selectRow(row);
}

void Rt60PaneView::selectRow(int row) {
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size() || binding_.settings == nullptr) return;
    binding_.settings->selectedBand = row;
    refreshChart();
    repaint();
}

// The chart follows the PICKED trace's curve, in the band the operator clicked;
// with no click, the band nearest 1 kHz that has a curve (a speech-range band
// is the one anybody looks at first).
void Rt60PaneView::refreshChart() {
    chartBand_ = -1;
    // The single result feeds the chart even while an ensemble fills the table.
    if (shown_ == nullptr || !shown_->analysed()) {
        chart_.clear();
        return;
    }
    const int count = static_cast<int>(shown_->bands.size());
    int index = binding_.settings != nullptr ? binding_.settings->selectedBand : -1;
    if (index < 0 || index >= count) {
        double bestDistance = 1e300;
        index = -1;
        for (int i = 0; i < count; ++i) {
            if (!shown_->bands[static_cast<std::size_t>(i)].edc.valid()) continue;
            const double distance =
                std::fabs(std::log(shown_->bands[static_cast<std::size_t>(i)].nominalHz / 1000.0));
            if (distance < bestDistance) {
                bestDistance = distance;
                index = i;
            }
        }
        if (index < 0) {
            chart_.clear();
            return;
        }
    }
    chartBand_ = index;
    const auto& band = shown_->bands[static_cast<std::size_t>(index)];
    const std::string name = binding_.selectedName ? binding_.selectedName() : std::string();
    if (!band.edc.valid()) {
        chart_.setCurve(band.edc, std::to_string(band.nominalHz) + " Hz: no decay curve (this band was refused)");
        return;
    }
    chart_.setCurve(band.edc, "EDC " + std::to_string(band.nominalHz) + " Hz  " + name);
}

void Rt60PaneView::paint(juce::Graphics& g) {
    g.fillAll(az::ui::background);
    lastPaintDrewTable_ = false;
    if (analysingShown_) {
        g.setColour(emptyStateText);
        g.setFont(az::ui::legendFont(az::ui::captionFontSize, true, az::ui::trackingCaption));
        g.drawFittedText("ANALYSING...", tableArea_.reduced(az::ui::gap * 2), juce::Justification::centred, 2);
        return;
    }
    const bool haveTable = !rows_.empty();
    if (!haveTable && wholeRefusal_.empty()) {
        g.setColour(emptyStateText);
        g.setFont(az::ui::legendFont(az::ui::captionFontSize, true, az::ui::trackingCaption));
        g.drawFittedText("PICK A SWEEP IR, OR PRESS SWEEP", tableArea_.reduced(az::ui::gap * 2),
                         juce::Justification::centred, 2);
        return;
    }
    if (!haveTable) {
        // The one refusal the pane has to show, whole -- not a table of dashes.
        g.setColour(az::ui::warn);
        g.setFont(az::ui::monoFont(az::ui::tableFontSize));
        g.drawFittedText(fromUtf8(wholeRefusal_), tableArea_.reduced(az::ui::gap * 2), juce::Justification::centredLeft,
                         4);
        return;
    }

    // Column heading, then one row per band. Numbers right-aligned in mono so the
    // decimal points line up; the note takes the remaining width.
    const int figureWidth = columnsAreEnsemble_ ? kEnsembleFigureWidth : kFigureWidth;
    auto area = tableArea_;
    const auto columns = [figureWidth](juce::Rectangle<int> row) {
        struct Cells {
            juce::Rectangle<int> hz, t30, t20, edt, range, note;
        } c;
        c.hz = row.removeFromLeft(kHzWidth);
        c.t30 = row.removeFromLeft(figureWidth);
        c.t20 = row.removeFromLeft(figureWidth);
        c.edt = row.removeFromLeft(figureWidth);
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
        const auto head = columns(area.removeFromTop(rowHeight_));
        g.setColour(az::ui::dim);
        g.setFont(az::ui::legendFont(az::ui::columnFontSize, true, az::ui::trackingColumn));
        for (const auto& [cell, text] : {std::pair{head.hz, "Hz"}, std::pair{head.t30, "T30"}, std::pair{head.t20, "T20"},
                                         std::pair{head.edt, "EDT*"},
                                         std::pair{head.range, columnsAreEnsemble_ ? "CAPTURES" : "RANGE"}}) {
            g.drawText(text, cell.withTrimmedRight(az::ui::gap), juce::Justification::centredRight, true);
        }
        g.drawText("NOTE", head.note, juce::Justification::centredLeft, true);
        g.setColour(az::ui::border);
        g.drawHorizontalLine(area.getY() - 1, static_cast<float>(tableArea_.getX()),
                             static_cast<float>(tableArea_.getRight()));
    }

    g.setFont(az::ui::monoFont(az::ui::tableFontSize));
    int index = 0;
    for (const auto& row : rows_) {
        const auto rowArea = area.removeFromTop(rowHeight_);
        if (index == chartBand_) {
            // The row the chart follows.
            g.setColour(az::ui::accent.withAlpha(0.12f));
            g.fillRect(rowArea);
        }
        ++index;
        const auto cells = columns(rowArea);
        const auto figureColour = [](const std::string& cell, juce::Colour normal) {
            return cell == "--" ? az::ui::faded : normal;
        };
        numberCell(cells.hz, fromUtf8(row.hz), az::ui::text);
        numberCell(cells.t30, fromUtf8(row.t30), figureColour(row.t30, az::ui::accent));
        numberCell(cells.t20, fromUtf8(row.t20), figureColour(row.t20, az::ui::text));
        numberCell(cells.edt, fromUtf8(row.edt), figureColour(row.edt, az::ui::dim));
        numberCell(cells.range, fromUtf8(row.range), figureColour(row.range, az::ui::dim));
        g.setColour(az::ui::warn);
        g.drawFittedText(fromUtf8(row.note), cells.note, juce::Justification::centredLeft, 1);
        g.setColour(az::ui::border.withAlpha(0.5f));
        g.drawHorizontalLine(cells.hz.getBottom(), static_cast<float>(tableArea_.getX()),
                             static_cast<float>(tableArea_.getRight()));
    }
    lastPaintDrewTable_ = true;
}

juce::PopupMenu Rt60PaneView::buildEnsembleMenu(std::vector<std::string>* idsByItem) const {
    juce::PopupMenu menu;
    const auto ticked = binding_.ensembleIds ? binding_.ensembleIds() : std::vector<std::string>{};
    const bool full = ticked.size() >= rta::measure::kRt60MaxEnsembleCaptures;
    std::vector<std::string> ids;
    for (const auto& choice : choices_) {
        // A trace with no IR cannot join: the picker already lists it with the reason.
        if (!choice.hasIr) continue;
        const bool isTicked = std::find(ticked.begin(), ticked.end(), choice.id) != ticked.end();
        ids.push_back(choice.id);
        menu.addItem(static_cast<int>(ids.size()), fromUtf8(choice.name), isTicked || !full, isTicked);
    }
    if (!ticked.empty()) {
        menu.addSeparator();
        ids.emplace_back();  // the empty id is "clear"
        menu.addItem(static_cast<int>(ids.size()), "Clear ensemble");
    }
    if (idsByItem != nullptr) *idsByItem = std::move(ids);
    return menu;
}

juce::StringArray Rt60PaneView::ensembleMenuForTest() const {
    std::vector<std::string> ids;
    auto menu = buildEnsembleMenu(&ids);
    const auto ticked = binding_.ensembleIds ? binding_.ensembleIds() : std::vector<std::string>{};
    juce::StringArray out;
    juce::PopupMenu::MenuItemIterator iterator(menu);
    while (iterator.next()) {
        const auto& item = iterator.getItem();
        if (item.isSeparator) continue;
        out.add((item.isTicked ? "[x] " : "[ ] ") + item.text + (item.isEnabled ? "" : " (full)"));
    }
    return out;
}

void Rt60PaneView::chooseEnsembleEntry(int index) {
    std::vector<std::string> ids;
    (void) buildEnsembleMenu(&ids);
    if (index < 0 || static_cast<std::size_t>(index) >= ids.size()) return;
    const std::string& id = ids[static_cast<std::size_t>(index)];
    if (id.empty()) {
        if (binding_.clearEnsemble) binding_.clearEnsemble();
    } else if (binding_.toggleEnsemble) {
        binding_.toggleEnsemble(id);
    }
    refresh();
}

}  // namespace rta::view
