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

}  // namespace rta::view
