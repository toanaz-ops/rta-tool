// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of az_ui -- the SODIUM RACK design system. No measurement vocabulary
// (see GridPanel.h's own header comment).
#include "GridPanel.h"

#include "Metrics.h"
#include "Palette.h"
#include "Typography.h"

#include <algorithm>

namespace az::ui {

GridPanel::GridPanel() = default;

void GridPanel::setColumnHeaders(std::vector<std::string> headers) {
    columnHeaders_ = std::move(headers);
    repaint();
}

void GridPanel::setRowHeaders(std::vector<std::string> headers) {
    rowHeaders_ = std::move(headers);
    repaint();
}

void GridPanel::setGridSize(int rows, int columns) {
    rows_ = std::max(0, rows);
    columns_ = std::max(0, columns);
    cellText_.assign(static_cast<std::size_t>(rows_) * static_cast<std::size_t>(columns_), {});
    repaint();
}

std::size_t GridPanel::index(int row, int column) const noexcept {
    return static_cast<std::size_t>(row) * static_cast<std::size_t>(columns_) +
          static_cast<std::size_t>(column);
}

void GridPanel::setCellText(int row, int column, std::string cellLabel) {
    if (row < 0 || row >= rows_ || column < 0 || column >= columns_) return;
    cellText_[index(row, column)] = std::move(cellLabel);
    repaint();
}

std::string GridPanel::cellText(int row, int column) const {
    if (row < 0 || row >= rows_ || column < 0 || column >= columns_) return {};
    return cellText_[index(row, column)];
}

void GridPanel::setSelectedCell(int row, int column) {
    selectedRow_ = row;
    selectedColumn_ = column;
    repaint();
}

GridPanel::Geometry GridPanel::computeGeometry() const {
    Geometry g;
    g.headerColumnWidth = rowHeaders_.empty() ? 0 : gutterWidth;
    g.headerRowHeight = columnHeaders_.empty() ? 0 : captionHeight;
    const int availableWidth = getWidth() - g.headerColumnWidth;
    const int availableHeight = getHeight() - g.headerRowHeight;
    g.cellWidth = columns_ > 0 ? std::max(0, availableWidth) / columns_ : 0;
    g.cellHeight = rows_ > 0 ? std::max(0, availableHeight) / rows_ : 0;
    return g;
}

std::pair<int, int> GridPanel::cellAt(juce::Point<int> localPoint) const {
    const Geometry g = computeGeometry();
    if (g.cellWidth <= 0 || g.cellHeight <= 0) return {-1, -1};

    const int x = localPoint.x - g.headerColumnWidth;
    const int y = localPoint.y - g.headerRowHeight;
    if (x < 0 || y < 0) return {-1, -1};

    const int column = x / g.cellWidth;
    const int row = y / g.cellHeight;
    if (column < 0 || column >= columns_ || row < 0 || row >= rows_) return {-1, -1};
    return {row, column};
}

void GridPanel::mouseDown(const juce::MouseEvent& event) {
    const auto [row, column] = cellAt(event.getPosition());
    if (row < 0 || column < 0) return;
    setSelectedCell(row, column);
    if (onCellClicked) onCellClicked(row, column);
}

void GridPanel::resized() {}

void GridPanel::paint(juce::Graphics& g) {
    const Geometry geometry = computeGeometry();
    g.fillAll(az::ui::panel);

    g.setFont(az::ui::legendFont(az::ui::columnFontSize, true, az::ui::trackingColumn));
    for (std::size_t c = 0; c < columnHeaders_.size() && static_cast<int>(c) < columns_; ++c) {
        const juce::Rectangle<int> cell(
            geometry.headerColumnWidth + static_cast<int>(c) * geometry.cellWidth, 0,
            geometry.cellWidth, geometry.headerRowHeight);
        g.setColour(az::ui::dim);
        g.drawText(juce::String(columnHeaders_[c]), cell, juce::Justification::centred, false);
    }
    for (std::size_t r = 0; r < rowHeaders_.size() && static_cast<int>(r) < rows_; ++r) {
        const juce::Rectangle<int> cell(0, geometry.headerRowHeight + static_cast<int>(r) * geometry.cellHeight,
                                        geometry.headerColumnWidth, geometry.cellHeight);
        g.setColour(az::ui::dim);
        g.drawText(juce::String(rowHeaders_[r]), cell, juce::Justification::centredLeft, false);
    }

    g.setFont(az::ui::monoFont(az::ui::tableFontSize));
    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < columns_; ++c) {
            const juce::Rectangle<int> cell(geometry.headerColumnWidth + c * geometry.cellWidth,
                                            geometry.headerRowHeight + r * geometry.cellHeight,
                                            geometry.cellWidth, geometry.cellHeight);
            const bool selected = r == selectedRow_ && c == selectedColumn_;
            g.setColour(selected ? az::ui::raise : az::ui::well);
            g.fillRect(cell.reduced(1));
            g.setColour(az::ui::border);
            g.drawRect(cell, 1);
            g.setColour(az::ui::text);
            g.drawText(juce::String(cellText(r, c)), cell, juce::Justification::centred, false);
        }
    }
}

}  // namespace az::ui
