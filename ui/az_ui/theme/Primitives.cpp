// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Primitives.h"

namespace az::ui
{

void drawEngravedDivider (juce::Graphics& g, const juce::Rectangle<int> band)
{
    // A groove is milled, not drawn on: one dark line, one faint light line
    // under it. Two 1 px rows is the whole trick, and it is what stops the
    // layout reading as a stack of flat boxes.
    const auto y = (float) band.getBottom();

    g.setColour (shade);
    g.fillRect ((float) band.getX(), y - 1.0f, (float) band.getWidth(), 1.0f);

    g.setColour (sheen);
    g.fillRect ((float) band.getX(), y, (float) band.getWidth(), 1.0f);
}

void drawWell (juce::Graphics& g, const juce::Rectangle<float> bounds, const bool focused)
{
    g.setColour (well);
    g.fillRoundedRectangle (bounds, cornerRadius);

    // A hairline of shadow along the top inside edge: the cheap read of
    // "recessed" without paying for a real inner blur on every combo.
    g.setColour (shade.withAlpha (0.6f));
    g.fillRect (bounds.getX() + cornerRadius, bounds.getY() + 1.0f,
                bounds.getWidth() - 2.0f * cornerRadius, 1.0f);

    g.setColour (focused ? accent : border);
    g.drawRoundedRectangle (bounds.reduced (0.5f), cornerRadius, 1.0f);
}

float stringWidth (const juce::Font& font, const juce::String& content)
{
    if (content.isEmpty())
        return 0.0f;

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, content, 0.0f, 0.0f);
    return glyphs.getBoundingBox (0, -1, true).getWidth();
}

juce::String elideMiddle (const juce::Font& font, const juce::String& content,
                          const float maxWidth)
{
    if (maxWidth <= 0.0f || stringWidth (font, content) <= maxWidth)
        return content;

    // Built from a code point: a literal ellipsis in a C++ source string can
    // come back as mojibake through MSVC's execution charset.
    static const juce::String ellipsis = juce::String::charToString ((juce::juce_wchar) 0x2026);

    // The tail is what identifies the port, so it is protected: keep the last
    // TWO characters ("1", " 1", "12") before giving up any of them.
    const int length = content.length();
    const int keepTail = juce::jmin (2, length);

    for (int head = length - keepTail - 1; head >= 1; --head)
    {
        const auto candidate = content.substring (0, head) + ellipsis
                             + content.substring (length - keepTail);

        if (stringWidth (font, candidate) <= maxWidth)
            return candidate;
    }

    // Nothing fits with a head at all -- show the tail alone rather than
    // something that could be any channel.
    return ellipsis + content.substring (length - keepTail);
}

void drawCaption (juce::Graphics& g, const juce::String& caption,
                  const juce::Rectangle<int> bounds, const juce::Colour colour)
{
    g.setColour (colour);
    g.setFont (legendFont (captionFontSize, true, trackingCaption));
    g.drawText (caption.toUpperCase(), bounds, juce::Justification::centredLeft, false);
}

} // namespace az::ui
