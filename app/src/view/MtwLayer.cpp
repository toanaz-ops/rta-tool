// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Task 7 of
// docs/plans/2026-09-05-L3-mtw-impl-plan.md.
#include "view/MtwLayer.h"

#include "view/CoherenceAlpha.h"

#include <cmath>

namespace rta::view {

std::vector<float> mtwColumnAlpha(const rta::measure::MtwBlock& block,
                                  const std::vector<int>& columns, std::size_t columnCount) {
    // Built once per point, not once per band: an ungated band's points read
    // 1.0 here (see this function's own header comment for why that is NOT
    // the coherence-alpha floor), and a gated band's points read its real,
    // already-gated coherence straight from the flat array Analyser::publish
    // wrote.
    std::vector<float> perPoint(block.frequencyHz.size(), 1.0f);
    for (const auto& band : block.bands) {
        if (!band.coherenceAvailable) continue;
        for (std::size_t i = band.firstIndex; i < band.firstIndex + band.pointCount; ++i) {
            if (i < block.coherence.size()) perPoint[i] = block.coherence[i];
        }
    }
    return columnAlpha(perPoint, columns, static_cast<int>(columnCount));
}

void drawMtwSeams(juce::Graphics& g, const rta::measure::MtwBlock& block,
                  const PlotGeometry& geometry, juce::Colour seamColour) {
    g.setColour(seamColour);
    for (const auto& band : block.bands) {
        // The bottom band's seamHz is 0 -- no lower neighbour to mark, and
        // xForHz(0) is non-finite in any case (guarded below too, so this
        // `continue` documents the intent rather than being load-bearing on
        // its own).
        if (!(band.seamHz > 0.0f)) continue;
        const float x = geometry.xForHz(static_cast<double>(band.seamHz));
        if (!std::isfinite(x)) continue;
        g.drawVerticalLine(static_cast<int>(std::floor(x)), geometry.top, geometry.bottom);
    }
}

}  // namespace rta::view
