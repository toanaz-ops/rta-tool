// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Station-4 fix pass, lane L3b finding 1.
#include "view/MtwReadout.h"

#include "view/MeasureColours.h"

#include <az_ui/az_ui.h>

#include <cmath>

namespace rta::view {

juce::String formatIntegrationSeconds(float seconds) {
    const double s = static_cast<double>(seconds);
    // See this file's header comment for why the threshold sits at 1 s
    // rather than following the project's dB rule.
    const int decimals = s >= 1.0 ? 1 : 2;
    return juce::String(s, decimals) + " s";
}

juce::String formatBandRange(const rta::measure::MtwBlock& block, std::size_t bandIndex) {
    const auto& bands = block.bands;
    if (bands.empty() || bandIndex >= bands.size()) return {};
    if (bands.size() == 1) return "ALL";

    const bool isBottom = bandIndex == 0;
    const bool isTop = bandIndex + 1 == bands.size();
    const int lowerHz = static_cast<int>(std::lround(bands[bandIndex].seamHz));

    if (isBottom) {
        const int upperHz = static_cast<int>(std::lround(bands[bandIndex + 1].seamHz));
        return "< " + juce::String(upperHz) + " Hz";
    }
    if (isTop) {
        return "> " + juce::String(lowerHz);
    }
    const int upperHz = static_cast<int>(std::lround(bands[bandIndex + 1].seamHz));
    return juce::String(lowerHz) + "-" + juce::String(upperHz);
}

juce::String mtwIntegrationStrip(const rta::measure::MtwBlock& block) {
    juce::String result;
    for (std::size_t i = 0; i < block.bands.size(); ++i) {
        if (i > 0) result << " | ";
        result << formatBandRange(block, i) << "  " << formatIntegrationSeconds(block.bands[i].integrationSeconds);
    }
    return result;
}

void drawMtwIntegrationStrip(juce::Graphics& g, const rta::measure::MtwBlock& block,
                             const PlotGeometry& geometry) {
    if (block.bands.empty()) return;

    g.setColour(rta::view::mtwReadout);
    g.setFont(az::ui::monoFont(az::ui::tableFontSize));
    const juce::Rectangle<int> line(static_cast<int>(geometry.left) + az::ui::spacing,
                                    static_cast<int>(geometry.top) + az::ui::spacing,
                                    static_cast<int>(geometry.right - geometry.left) - 2 * az::ui::spacing,
                                    az::ui::captionHeight);
    g.drawText(mtwIntegrationStrip(block), line, juce::Justification::centredLeft, false);
}

}  // namespace rta::view
