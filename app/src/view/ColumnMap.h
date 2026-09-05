// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Task 7 of
// docs/plans/2026-09-05-L3-mtw-impl-plan.md.
#pragma once

#include "view/PlotGeometry.h"

#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace rta::view {

namespace detail {

/// The floor of the axis's own left edge, below which a column belongs to
/// the level-label gutter and must not be reported as inside the plot
/// (originally TransferView.cpp's own comment on `absoluteColumnsForBins`,
/// unchanged by this move: `geometry.left` is the same `axis.left` every
/// pane's geometry carries, and StoredTraceLayer.cpp's `columnsForBins`
/// floors that identical value into its own origin and rejects anything
/// below it -- this floor is that same rejection).
[[nodiscard]] inline double columnMapLeftEdge(const PlotGeometry& geometry) noexcept {
    return std::floor(static_cast<double>(geometry.left));
}

/// The ONE per-element mapping both public functions below call -- so a bin
/// width and an explicit frequency vector cannot drift apart on the same
/// geometry, which is the entire point of the refactor that created this
/// file. `hz <= 0` (the MTW block's own DC entry, or an unmapped bin) and any
/// non-finite `xForHz` result (`xForHz(0)` is `log(0)`, non-finite) both
/// answer -1 rather than reaching a `juce::Path` with garbage -- the caller
/// must never see a column outside `[0, columnCount)`.
[[nodiscard]] inline int columnForHz(const PlotGeometry& geometry, double hz, double leftEdge,
                                     std::size_t columnCount) noexcept {
    if (!(hz > 0.0)) return -1;
    const double x = geometry.xForHz(hz);
    if (!std::isfinite(x)) return -1;
    const double column = std::floor(x);
    if (column >= leftEdge && column < static_cast<double>(columnCount)) {
        return static_cast<int>(column);
    }
    return -1;
}

}  // namespace detail

/// One column per FFT bin, `bin * binHz` each -- the fixed engine's own axis.
[[nodiscard]] inline std::vector<int> absoluteColumnsForBins(const PlotGeometry& geometry,
                                                             double binHz, std::size_t bins,
                                                             std::size_t columnCount) {
    std::vector<int> columns(bins, -1);
    const double leftEdge = detail::columnMapLeftEdge(geometry);
    for (std::size_t i = 0; i < bins; ++i) {
        columns[i] = detail::columnForHz(geometry, static_cast<double>(i) * binHz, leftEdge,
                                         columnCount);
    }
    return columns;
}

/// One column per entry of an EXPLICIT frequency vector -- the MTW block's
/// own axis, which is not a uniform bin width (MtwLayout.h: each band's own
/// bin frequency, never the top band's alone).
[[nodiscard]] inline std::vector<int> absoluteColumnsForFrequencies(const PlotGeometry& geometry,
                                                                    std::span<const double> hz,
                                                                    std::size_t columnCount) {
    std::vector<int> columns(hz.size(), -1);
    const double leftEdge = detail::columnMapLeftEdge(geometry);
    for (std::size_t i = 0; i < hz.size(); ++i) {
        columns[i] = detail::columnForHz(geometry, hz[i], leftEdge, columnCount);
    }
    return columns;
}

}  // namespace rta::view
