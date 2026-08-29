// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Uses JUCE (juce::Graphics), so it is NOT in
// the measure_has_no_framework_deps file list. Decision 3 of
// docs/dsp/2026-08-29-display-layer-l5c.md, drawing half.
#include "view/TraceStroke.h"

#include <algorithm>

namespace rta::view {

namespace {

/// Alpha for column `c`: the caller's own trust, or fully opaque when the
/// span has nothing to say about that column. An EMPTY span means "no
/// coherence was ever measured for this trace" (the contract in
/// TraceStroke.h); a span shorter than the columns being drawn reads the
/// same way for every column past its end, rather than indexing out of
/// bounds.
[[nodiscard]] float alphaFor(std::span<const float> columnAlpha, std::size_t column) noexcept {
    return column < columnAlpha.size() ? columnAlpha[column] : 1.0f;
}

/// Fill one column's y-extent in the geometry's own linear axis. `yForDb` is
/// named for dB but is just PlotGeometry's one linear-axis mapper -- reused
/// here for degrees too, because a phase pane's `PlotGeometry` is built with
/// dbTop/dbBottom set to +-180 (see BodeLayout.h::paneGeometry) rather than a
/// second mapping function existing for the same job.
void fillColumn(juce::Graphics& g, std::size_t column, float lo, float hi,
               const PlotGeometry& geometry, int originY) {
    // yForDb is top-down, so the MAXIMUM value is the SMALLER y.
    const float yTop = geometry.yForDb(static_cast<double>(hi)) - static_cast<float>(originY);
    const float yBottom = geometry.yForDb(static_cast<double>(lo)) - static_cast<float>(originY);

    // A column whose min and max coincide is a flat span, not an absence.
    // Give it a full pixel or the trace vanishes wherever it is level.
    const float height = std::max(1.0f, yBottom - yTop);
    g.fillRect(static_cast<float>(column), yTop, 1.0f, height);
}

}  // namespace

void strokeMagnitudeExtents(juce::Graphics& g, std::span<const ColumnExtent> extents,
                            const PlotGeometry& geometry, int originY,
                            std::span<const float> columnAlpha, juce::Colour base) {
    for (std::size_t c = 0; c < extents.size(); ++c) {
        const auto& extent = extents[c];
        if (!extent.hasData) continue;

        // yForDb CLAMPS, so a column whose peak never reaches the plot's
        // bottom dB would otherwise draw as a point pinned to the floor --
        // asserting a measurement AT the floor that was never taken. Skip the
        // whole column instead: nothing in its measured range was visible, so
        // nothing should be drawn for it.
        if (static_cast<double>(extent.maxValue) < geometry.dbBottom) continue;

        g.setColour(base.withMultipliedAlpha(alphaFor(columnAlpha, c)));
        fillColumn(g, c, extent.minValue, extent.maxValue, geometry, originY);
    }
}

void strokePhaseColumns(juce::Graphics& g, std::span<const DrawnPhaseColumn> columns,
                        const PlotGeometry& geometry, int originY,
                        std::span<const float> columnAlpha, juce::Colour base) {
    for (std::size_t c = 0; c < columns.size(); ++c) {
        const auto& column = columns[c];
        if (!column.hasData) continue;

        g.setColour(base.withMultipliedAlpha(alphaFor(columnAlpha, c)));

        // A straddle is the union of [minDeg, +180] and [-180, maxDeg] -- two
        // short pieces at the top and bottom edges of the pane. minDeg >
        // maxDeg here (that IS what "straddles" means after wrapping), so
        // filling [minDeg, maxDeg] as one rect -- the tempting "simplification"
        // -- would either draw an empty/negative-height rect or, worse, get
        // "corrected" by swapping the two and painting the ~358-degree band
        // across the MIDDLE of the pane: the wrap artefact this whole
        // mechanism exists to avoid. The gap between the two pieces is the
        // point, not a shortfall to be closed.
        if (column.straddlesWrap) {
            fillColumn(g, c, column.minDeg, 180.0f, geometry, originY);
            fillColumn(g, c, -180.0f, column.maxDeg, geometry, originY);
        } else {
            // fullBand also lands here: wrapForDrawing already set
            // minDeg/maxDeg to -180/180 for that case, so the plain path
            // draws the whole pane height with no special case needed.
            fillColumn(g, c, column.minDeg, column.maxDeg, geometry, originY);
        }
    }
}

}  // namespace rta::view
