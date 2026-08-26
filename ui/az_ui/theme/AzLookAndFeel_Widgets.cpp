// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AzLookAndFeel.h"

#include "Palette.h"
#include "Metrics.h"
#include "Typography.h"
#include "Primitives.h"

namespace az::ui
{

//==============================================================================

juce::Font AzLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    // Combo contents are values -- rates, channel names, dB. Mono so a column
    // of them lines up and never jitters as the selection changes.
    return monoFont (baseFontSize - 1.0f);
}

void AzLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    // The label is given a ONE-LINE height, centred, not the combo's full
    // height. juce::Label fits as many lines as its height allows, so a 26 px
    // combo holding a 13 px face wrapped "Analogue 1" onto two lines and
    // showed "Analogue" over "1". A value that wraps is a value you cannot
    // read at a glance, which is the entire job of this control.
    const auto font = getComboBoxFont (box);
    const int  lineHeight = juce::roundToInt (font.getHeight()) + 2;

    label.setBounds (gap, (box.getHeight() - lineHeight) / 2,
                     juce::jmax (1, box.getWidth() - gap - (int) (box.getHeight() * 0.6f)),
                     lineHeight);
    label.setFont (font);
    label.setMinimumHorizontalScale (0.85f);
}

void AzLookAndFeel::drawComboBox (juce::Graphics& g, const int width, const int height,
                                  const bool isButtonDown, const int buttonX, int,
                                  const int buttonW, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

    drawWell (g, bounds, isButtonDown || box.hasKeyboardFocus (false));

    // A small caret, centred in the button area -- no JUCE default chrome.
    juce::Path caret;
    const float cx = (float) buttonX + ((float) buttonW * 0.5f);
    const float cy = (float) height * 0.5f;
    const float r  = 3.5f;
    caret.addTriangle (cx - r, cy - r * 0.55f, cx + r, cy - r * 0.55f, cx, cy + r * 0.65f);

    g.setColour (box.findColour (box.isEnabled() ? juce::ComboBox::arrowColourId
                                                 : juce::ComboBox::arrowColourId)
                     .withAlpha (box.isEnabled() ? 1.0f : 0.35f));
    g.fillPath (caret);
}

//==============================================================================

void AzLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                      const bool shouldDrawButtonAsHighlighted, bool)
{
    // A hardware toggle: a recessed track with a knob that travels. A JUCE
    // tick box is 18 px of ambiguity at arm's length; this reads as on/off
    // from its SHAPE, which is what survives a glance in a dark room.
    constexpr float trackW = 32.0f;
    constexpr float trackH = 16.0f;
    constexpr float inset  = 2.0f;

    const bool on = button.getToggleState();
    const auto track = juce::Rectangle<float> (trackW, trackH)
                           .withCentre ({ (float) inset + trackW * 0.5f,
                                          (float) button.getHeight() * 0.5f });

    g.setColour (on ? accent.withAlpha (0.28f) : well);
    g.fillRoundedRectangle (track, trackH * 0.5f);

    g.setColour (on ? accent.withAlpha (0.8f)
                    : (shouldDrawButtonAsHighlighted ? border.brighter (0.3f) : border));
    g.drawRoundedRectangle (track.reduced (0.5f), trackH * 0.5f, 1.0f);

    const float knobD = trackH - 2.0f * inset;
    const float knobX = on ? track.getRight() - inset - knobD : track.getX() + inset;

    g.setColour (on ? accent : dim);
    g.fillEllipse (knobX, track.getY() + inset, knobD, knobD);

    if (button.getButtonText().isNotEmpty())
    {
        g.setColour (button.findColour (juce::ToggleButton::textColourId)
                         .withAlpha (button.isEnabled() ? 1.0f : 0.4f));
        g.setFont (baseFont());
        g.drawText (button.getButtonText(),
                    button.getLocalBounds().withTrimmedLeft ((int) (track.getRight() + gap)),
                    juce::Justification::centredLeft, false);
    }
}

//==============================================================================

void AzLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    g.setColour (label.findColour (juce::Label::textColourId)
                     .withAlpha (label.isEnabled() ? 1.0f : 0.4f));

    const auto font = getLabelFont (label);
    g.setFont (font);

    auto textArea = label.getBorderSize().subtractedFrom (label.getLocalBounds());

    // A ComboBox's own label holds a VALUE -- a channel name, a rate, a buffer
    // size -- and those identify themselves by their tail. Elide the middle so
    // the port number survives; every other label truncates normally.
    const auto shown = dynamic_cast<juce::ComboBox*> (label.getParentComponent()) != nullptr
                          ? elideMiddle (font, label.getText(), (float) textArea.getWidth())
                          : label.getText();

    g.drawFittedText (shown, textArea,
                      label.getJustificationType(),
                      juce::jmax (1, (int) ((float) textArea.getHeight() / label.getFont().getHeight())),
                      label.getMinimumHorizontalScale());
}

juce::Font AzLookAndFeel::getLabelFont (juce::Label& label)
{
    // A label that a component has already given a font keeps it -- panels set
    // mono on their numeric readouts and legend on their captions, and this
    // must not overwrite that decision.
    return label.getFont();
}

//==============================================================================

void AzLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, const int width, const int height)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

    g.setColour (panel);
    g.fillRoundedRectangle (bounds, cornerRadius);
    g.setColour (border);
    g.drawRoundedRectangle (bounds.reduced (0.5f), cornerRadius, 1.0f);
}

juce::Font AzLookAndFeel::getPopupMenuFont()
{
    return monoFont (baseFontSize - 1.0f);
}

//==============================================================================

void AzLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&,
                                   const int x, const int y, const int width, const int height,
                                   const bool isScrollbarVertical,
                                   const int thumbStartPosition, const int thumbSize,
                                   const bool isMouseOver, const bool isMouseDown)
{
    if (thumbSize <= 0)
        return;

    auto thumb = isScrollbarVertical
                     ? juce::Rectangle<int> (x, thumbStartPosition, width, thumbSize).reduced (3, 1)
                     : juce::Rectangle<int> (thumbStartPosition, y, thumbSize, height).reduced (1, 3);

    g.setColour (isMouseDown ? accent.withAlpha (0.7f)
                             : (isMouseOver ? border.brighter (0.5f) : border));
    g.fillRoundedRectangle (thumb.toFloat(), thumb.toFloat().getWidth() * 0.5f);
}

} // namespace az::ui
