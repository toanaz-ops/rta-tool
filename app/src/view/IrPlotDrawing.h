// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Internal to IrEtcPlot.cpp and IrGatedPlot.cpp (see IrPlots.h):
// the plot margins and the empty-state picture the two IR pane plots share.
#pragma once

#include "view/MeasureColours.h"

#include <az_ui/az_ui.h>

#include <juce_gui_basics/juce_gui_basics.h>

namespace rta::view::detail {

inline constexpr int kMarginLeft = 44;
inline constexpr int kMarginRight = 10;
inline constexpr int kMarginTop = 14;
inline constexpr int kMarginBottom = 20;

inline void drawEmpty(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& text) {
    g.fillAll(az::ui::background);
    g.setColour(emptyStateText);
    g.setFont(az::ui::legendFont(az::ui::captionFontSize, true, az::ui::trackingCaption));
    g.drawFittedText(text, bounds.reduced(az::ui::gap * 2), juce::Justification::centred, 3);
}

}  // namespace rta::view::detail
