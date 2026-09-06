// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of az_ui -- the SODIUM RACK design system. Generic row x column
// geometry and hit-testing only: NO measurement vocabulary (project
// CLAUDE.md, "Module boundaries") -- no "reference", "measurement",
// "coherence" or "trim" anywhere in this file. What a cell MEANS is the
// host application's business (see app/src/view/RoutingMatrix.h, which
// composes this).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace az::ui {

/// A generic grid of cells with an optional header row and header column --
/// a routing matrix, a patch bay, any "pick a row, pick a column" control is
/// one layer up. Geometry only: text and highlight are set per cell, but
/// this class assigns no meaning to any of it.
class GridPanel final : public juce::Component {
public:
    GridPanel();

    void setColumnHeaders(std::vector<std::string> headers);
    void setRowHeaders(std::vector<std::string> headers);

    /// Data-cell counts, independent of the header lists' own sizes -- a
    /// caller may size the grid before it has header text, or after.
    void setGridSize(int rows, int columns);

    void setCellText(int row, int column, std::string cellLabel);
    [[nodiscard]] std::string cellText(int row, int column) const;

    /// -1 for either argument clears the selection; drawing only, no click
    /// semantics of its own.
    void setSelectedCell(int row, int column);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

    /// Hit-tests a point in local coordinates against the DATA cell grid
    /// ONLY (never a header) -- {-1,-1} for a miss (outside the grid, or
    /// inside a header band). Exposed as production API so a test can drive
    /// it directly, the same trade TransferView's panes()/sourceToggle()
    /// already make.
    [[nodiscard]] std::pair<int, int> cellAt(juce::Point<int> localPoint) const;

    std::function<void(int row, int column)> onCellClicked;

private:
    struct Geometry {
        int headerColumnWidth = 0;
        int headerRowHeight = 0;
        int cellWidth = 0;
        int cellHeight = 0;
    };
    [[nodiscard]] Geometry computeGeometry() const;
    [[nodiscard]] std::size_t index(int row, int column) const noexcept;

    std::vector<std::string> columnHeaders_;
    std::vector<std::string> rowHeaders_;
    int rows_ = 0;
    int columns_ = 0;
    std::vector<std::string> cellText_;
    int selectedRow_ = -1;
    int selectedColumn_ = -1;
};

}  // namespace az::ui
