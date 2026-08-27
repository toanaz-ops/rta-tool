// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/dev/preview. See
// docs/specs/2026-08-28-interactive-tuning-visuals.md.
#include "dev/preview/PreviewFurniture.h"

#include "view/MeasureColours.h"

#include <az_ui/az_ui.h>

namespace rta::dev::preview {

void drawMasthead(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                   const juce::String& subtitle) {
    auto rule = area.removeFromBottom(2);

    const auto brandFont = az::ui::legendFont(az::ui::switchFontSize, true, az::ui::trackingCaption);
    g.setColour(az::ui::text);
    g.setFont(brandFont);
    g.drawText(title, area, juce::Justification::centredLeft);

    auto after = area;
    after.removeFromLeft(static_cast<int>(az::ui::stringWidth(brandFont, title)) + az::ui::gap * 3);
    g.setColour(az::ui::dim);
    g.setFont(az::ui::legendFont(az::ui::columnFontSize, false, az::ui::trackingColumn));
    g.drawText(subtitle, after, juce::Justification::centredLeft);

    az::ui::drawEngravedDivider(g, rule);
}

void drawChip(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& legend,
              const juce::String& value, juce::Colour valueColour, float valueHeight) {
    az::ui::drawWell(g, bounds.toFloat(), false);

    auto inner = bounds.reduced(az::ui::gap, 0);
    const auto legendFont = az::ui::legendFont(az::ui::columnFontSize, true, az::ui::trackingColumn);
    const auto valueFont = az::ui::monoFont(valueHeight);

    // Legend above, value below -- a chip is read as one unit from six feet,
    // so the two lines split the cell vertically rather than compete for the
    // same baseline the way DevicePanel's inline "DROPS  12" legend does;
    // this cell carries more text ("PEQ 250 Hz -4.0 dB Q 2.2") than a single
    // number and needs the extra line to stay legible at chip width.
    auto legendArea = inner.removeFromTop(juce::jmin(inner.getHeight() / 2,
                                                    (int) az::ui::captionHeight / 2 + 4));
    g.setColour(az::ui::faded);
    g.setFont(legendFont);
    g.drawText(legend, legendArea, juce::Justification::centredLeft, false);

    g.setColour(valueColour);
    g.setFont(valueFont);
    g.drawText(value, inner, juce::Justification::centredLeft, true);
}

void drawHatch(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour) {
    if (area.getWidth() <= 0.0f || area.getHeight() <= 0.0f) return;

    juce::Graphics::ScopedSaveState saveState(g);
    g.reduceClipRegion(area.getSmallestIntegerContainer());

    g.setColour(colour.withAlpha(0.5f));
    constexpr float kStep = 7.0f;  // hairline pitch: dense enough to read as
                                    // a texture from six feet, sparse enough
                                    // that the curve underneath still shows.
    const float diagonal = area.getWidth() + area.getHeight();
    for (float offset = 0.0f; offset < diagonal; offset += kStep) {
        const float x1 = area.getX() + offset;
        const float y1 = area.getY();
        const float x2 = area.getX();
        const float y2 = area.getY() + offset;
        g.drawLine(x1, y1, x2, y2, 1.0f);
    }
}

}  // namespace rta::dev::preview
