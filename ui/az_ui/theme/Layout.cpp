// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Layout.h"

#include <algorithm>
#include <cmath>

namespace az::ui {

std::vector<juce::Rectangle<int>> splitVertically(juce::Rectangle<int> area,
                                                  std::span<const float> weights, int gapPx) {
    std::vector<juce::Rectangle<int>> out;
    if (weights.empty()) return out;
    out.reserve(weights.size());

    const int gapTotal = gapPx * (static_cast<int>(weights.size()) - 1);
    const int usable = std::max(0, area.getHeight() - gapTotal);

    double weightTotal = 0.0;
    for (const float w : weights) weightTotal += std::max(0.0f, w);

    // Every weight non-positive is not a caller error to refuse -- it is
    // "nobody expressed a preference", and equal shares is the only answer
    // that does not privilege one pane for no stated reason.
    const bool equalShares = weightTotal <= 0.0;
    if (equalShares) weightTotal = static_cast<double>(weights.size());

    double consumed = 0.0;
    int y = area.getY();
    for (std::size_t i = 0; i < weights.size(); ++i) {
        const double w = equalShares ? 1.0 : static_cast<double>(std::max(0.0f, weights[i]));
        const double before = consumed;
        consumed += w;

        // Both edges from the running fraction, so child i's bottom and child
        // i+1's top are derived from the SAME accumulated number and cannot
        // disagree by a rounding step.
        const int top = static_cast<int>(std::llround(before / weightTotal * usable));
        const int bottom = static_cast<int>(std::llround(consumed / weightTotal * usable));
        const int height = std::max(0, bottom - top);

        out.emplace_back(area.getX(), y, area.getWidth(), height);
        y += height + gapPx;
    }
    return out;
}

}  // namespace az::ui
