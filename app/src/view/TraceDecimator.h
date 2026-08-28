// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. See
// docs/specs/2026-08-28-trace-library-and-session.md §4.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace rta::view {

/// What one pixel column of a trace covers. `hasData` is not redundant with a
/// zero range: a column no bin lands in must draw NOTHING. Drawing it as 0 dB
/// would put a line where there is no measurement.
struct ColumnExtent {
    float minValue = 0.0f;
    float maxValue = 0.0f;
    bool hasData = false;
};

/// Reduce per-bin values to one min/max extent per pixel column.
///
/// Min/max rather than one representative sample per column, because a narrow
/// peak or null falling between representatives simply disappears -- and a null
/// is what the engineer is hunting. `columnForBin` comes from the plot's own
/// log-frequency mapping, so this stays free of geometry and testable on bare
/// arrays.
[[nodiscard]] inline std::vector<ColumnExtent> decimateToColumns(
    std::span<const float> values, std::span<const int> columnForBin, int columnCount) {
    if (columnCount <= 0 || values.empty() || values.size() != columnForBin.size()) {
        return {};
    }

    std::vector<ColumnExtent> out(static_cast<std::size_t>(columnCount));
    for (std::size_t i = 0; i < values.size(); ++i) {
        const int column = columnForBin[i];
        if (column < 0 || column >= columnCount) continue;  // caller's mapping, not our crash

        auto& extent = out[static_cast<std::size_t>(column)];
        const float v = values[i];
        if (!extent.hasData) {
            extent.minValue = v;
            extent.maxValue = v;
            extent.hasData = true;
        } else {
            if (v < extent.minValue) extent.minValue = v;
            if (v > extent.maxValue) extent.maxValue = v;
        }
    }
    return out;
}

/// Fill columns that no bin landed in by interpolating between the nearest
/// columns that did.
///
/// Below roughly 2 kHz an FFT has fewer bins than the plot has pixel columns,
/// so `decimateToColumns` correctly reports most low columns as empty and the
/// trace draws as a dotted scatter. A spectrum is continuous and its bins are
/// samples of it, so joining them asserts LESS than leaving holes, which an
/// engineer reads as missing data.
///
/// Leading and trailing empty runs are left empty: outside the measured range
/// there is nothing to interpolate between, and inventing a value there would
/// be the very assertion this function exists to avoid making.
[[nodiscard]] inline std::vector<ColumnExtent> bridgeGaps(std::vector<ColumnExtent> columns) {
    const std::size_t n = columns.size();
    std::size_t i = 0;
    while (i < n) {
        if (columns[i].hasData) {
            ++i;
            continue;
        }

        // [gapStart, gapEnd) is one run of empty columns. `gapEnd` is either
        // the index of the next filled column, or `n` if the run reaches the
        // end -- both cases are detected by the loop below alone, with no
        // separate bounds check needed.
        const std::size_t gapStart = i;
        while (i < n && !columns[i].hasData) ++i;
        const std::size_t gapEnd = i;

        // A run with nothing filled on one side is leading or trailing: there
        // is no measured value on that side to interpolate FROM, so filling it
        // would invent data rather than join two real ones.
        if (gapStart == 0 || gapEnd == n) continue;

        const ColumnExtent& before = columns[gapStart - 1];
        const ColumnExtent& after = columns[gapEnd];
        const auto span = static_cast<float>(gapEnd - gapStart + 1);
        for (std::size_t k = gapStart; k < gapEnd; ++k) {
            const float t = static_cast<float>(k - gapStart + 1) / span;
            ColumnExtent filled;
            filled.minValue = before.minValue + t * (after.minValue - before.minValue);
            filled.maxValue = before.maxValue + t * (after.maxValue - before.maxValue);
            filled.hasData = true;
            columns[k] = filled;
        }
    }
    return columns;
}

}  // namespace rta::view
