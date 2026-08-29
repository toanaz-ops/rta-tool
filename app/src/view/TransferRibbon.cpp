// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Decision 3 of
// docs/dsp/2026-08-29-display-layer-l5c.md.
#include "view/TransferRibbon.h"

#include "view/CoherenceAlpha.h"
#include "view/MeasureColours.h"

#include <az_ui/az_ui.h>

#include <algorithm>
#include <cmath>

namespace rta::view {

void drawTransferRibbon(juce::Graphics& g, const PlotGeometry& geometry, PaneRect ribbonArea,
                        std::span<const float> coherence, std::span<const int> columnForBin,
                        int columnCount) {
    // "COH" caption in the same label-margin column drawLevelLabels reserves
    // for the panes below it -- one strip of furniture reading consistently
    // top to bottom.
    g.setColour(az::ui::faded);
    g.setFont(az::ui::legendFont(az::ui::columnFontSize, true, az::ui::trackingColumn));
    juce::Rectangle<int> caption(ribbonArea.x, ribbonArea.y, kLevelLabelWidth, ribbonArea.height);
    g.drawText("COH", caption, juce::Justification::centredRight, false);

    if (!coherence.empty()) {
        // One filled pixel column per pixel, alpha set by the per-column
        // MINIMUM gamma^2 -- trust shown never exceeds trust measured
        // (CoherenceAlpha.h). `columnAlpha` already applies
        // `alphaForCoherence`'s floor/monotone mapping; this loop only picks
        // the colour and paints.
        const auto alpha = columnAlpha(coherence, columnForBin, columnCount);
        const int left = static_cast<int>(std::floor(geometry.left));
        const int right = static_cast<int>(std::ceil(geometry.right));
        for (int x = left; x < right; ++x) {
            if (x < 0 || x >= columnCount) continue;
            g.setColour(rta::view::trace.withAlpha(alpha[static_cast<std::size_t>(x)]));
            g.fillRect(x, ribbonArea.y, 1, ribbonArea.height);
        }
    }

    g.setColour(rta::view::grid);
    g.drawRect(juce::Rectangle<float>(geometry.left, static_cast<float>(ribbonArea.y),
                                      geometry.right - geometry.left,
                                      static_cast<float>(ribbonArea.height)),
               1.0f);
}

}  // namespace rta::view
