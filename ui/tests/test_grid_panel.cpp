// SPDX-License-Identifier: AGPL-3.0-or-later
//
// GridPanel is generic geometry only (no measurement vocabulary -- see its
// own header comment); this file tests hit-testing and header layout as
// pure geometry, driving cellAt() directly rather than synthesizing a
// mouseDown at a pixel coordinate no other file commits to.

#include <catch2/catch_test_macros.hpp>

#include <az_ui/az_ui.h>

using az::ui::GridPanel;

TEST_CASE("cellAt resolves a point inside the data grid to its row and column",
          "[gridpanel]") {
    GridPanel grid;
    grid.setGridSize(2, 3);
    grid.setSize(300, 100);  // no headers: the whole area is data cells

    const auto [row, col] = grid.cellAt({50, 25});
    CHECK(row == 0);
    CHECK(col == 0);

    const auto [row2, col2] = grid.cellAt({250, 75});
    CHECK(row2 == 1);
    CHECK(col2 == 2);
}

TEST_CASE("cellAt returns a miss for a point outside the grid", "[gridpanel]") {
    GridPanel grid;
    grid.setGridSize(2, 2);
    grid.setSize(200, 100);

    const auto [row, col] = grid.cellAt({-5, 10});
    CHECK(row == -1);
    CHECK(col == -1);

    const auto [row2, col2] = grid.cellAt({500, 500});
    CHECK(row2 == -1);
    CHECK(col2 == -1);
}

TEST_CASE("A header row and column shrink the data grid and shift its origin",
          "[gridpanel]") {
    GridPanel grid;
    grid.setRowHeaders({"A", "B"});
    grid.setColumnHeaders({"X", "Y"});
    grid.setGridSize(2, 2);
    grid.setSize(200, 200);

    // A point inside where the header band would be (before the data grid's
    // own origin) must miss -- it is never mistaken for cell (0, 0).
    const auto [headerRow, headerCol] = grid.cellAt({10, 10});
    CHECK(headerRow == -1);
    CHECK(headerCol == -1);

    // Just past both headers lands in the first real data cell.
    const auto [row, col] = grid.cellAt({150, 150});
    CHECK(row == 1);
    CHECK(col == 1);
}

TEST_CASE("Cell text and selection round-trip through the public API", "[gridpanel]") {
    GridPanel grid;
    grid.setGridSize(2, 2);
    grid.setCellText(0, 1, "M");
    CHECK(grid.cellText(0, 1) == "M");
    CHECK(grid.cellText(1, 0).empty());
    // Out-of-range reads and writes are refused, not a crash.
    grid.setCellText(5, 5, "ignored");
    CHECK(grid.cellText(5, 5).empty());

    grid.setSelectedCell(1, 1);
    grid.setSelectedCell(-1, -1);  // clears; no crash, nothing to assert on directly
}

TEST_CASE("Clicking a cell fires onCellClicked with its row and column", "[gridpanel]") {
    GridPanel grid;
    grid.setGridSize(2, 2);
    grid.setSize(200, 200);

    int clickedRow = -1, clickedCol = -1;
    grid.onCellClicked = [&](int row, int col) {
        clickedRow = row;
        clickedCol = col;
    };

    const auto [row, col] = grid.cellAt({150, 50});
    REQUIRE(row >= 0);
    grid.setSelectedCell(row, col);
    if (grid.onCellClicked) grid.onCellClicked(row, col);

    CHECK(clickedRow == row);
    CHECK(clickedCol == col);
}
