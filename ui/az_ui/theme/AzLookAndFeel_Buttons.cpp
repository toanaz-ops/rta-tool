// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AzLookAndFeel.h"

#include "Palette.h"
#include "Metrics.h"
#include "Typography.h"
#include "Primitives.h"

namespace az::ui
{

namespace
{
// A switch that is latched ON sits one value step brighter than an idle one,
// with a brighter edge. Small numbers on purpose: the lamp does the shouting.
constexpr float kOnLift        = 0.035f;
constexpr float kHoverLift     = 0.05f;

// How much of the switch face the lamp's halo bleeds into.
constexpr float kLampGlowSpan  = 22.0f;

// How far a legend may be condensed before it is allowed to ellipsise. A
// tracked uppercase legend that overruns its cell by a few percent should
// squeeze; a destructive control truncating to something misleading is not
// an acceptable fallback.
constexpr float kLegendMinScale = 0.8f;

// The component property a caller sets to opt a button out of the default
// switch treatment. Set it with:  button.getProperties().set (azStyleProperty, "danger");
const juce::Identifier azStyleProperty { "azStyle" };
const juce::String     styleDanger     { "danger" };
const juce::String     styleGhost      { "ghost" };
const juce::String     styleSegment    { "segment" };

juce::String styleOf (const juce::Button& b)
{
    return b.getProperties().getWithDefault (azStyleProperty, juce::String()).toString();
}
} // namespace

//==============================================================================

void AzLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                          const juce::Colour& backgroundColour,
                                          const bool shouldDrawButtonAsHighlighted,
                                          const bool shouldDrawButtonAsDown)
{
    // Non-const: the lamp branch below carves its bar off the top with
    // removeFromTop, which mutates.
    auto       bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto style  = styleOf (button);
    const bool on     = button.getToggleState();

    // Destructive: never a filled slab. A control that looks like every
    // other switch is a control somebody eventually hits by accident, so it
    // is drawn as an outline that only fills once the pointer is on it.
    if (style == styleDanger)
    {
        if (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown)
        {
            g.setColour (danger.withAlpha (shouldDrawButtonAsDown ? 0.22f : 0.13f));
            g.fillRoundedRectangle (bounds, cornerRadius);
        }

        g.setColour (danger.withAlpha (shouldDrawButtonAsHighlighted ? 1.0f : 0.45f));
        g.drawRoundedRectangle (bounds, cornerRadius, 1.0f);
        return;
    }

    // Segment: one option inside a SegmentedControl. It paints a FILL and
    // nothing else -- the box around the group and the dividers between the
    // options belong to the group, which draws them itself. A per-button
    // border here is exactly what made the toolbar read as several separate
    // controls instead of one choice.
    if (style == styleSegment)
    {
        if (on)
        {
            // FILL ONLY. The selected segment's accent edge is drawn by the
            // SegmentedControl in paintOverChildren, after the dividers --
            // drawn here it was painted over on whichever side a divider fell,
            // which is why the highlight came out missing an edge.
            g.setColour (raise.brighter (0.10f));
            g.fillRect (button.getLocalBounds());
        }
        else if (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown)
        {
            g.setColour (raise.withAlpha (0.45f));
            g.fillRect (button.getLocalBounds());
        }
        return;
    }

    // Ghost: the small chips in a toolbar. Flat, no lamp -- they are display
    // options, not transport, and must not compete with the transport.
    //
    // A ghost chip that is ON takes its edge from buttonOnColourId, so a chip
    // whose state has a colour meaning elsewhere on screen can say so, while
    // a plain segmented chip just brightens.
    if (style == styleGhost)
    {
        if (on || shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown)
        {
            g.setColour (on ? raise : raise.withAlpha (0.55f));
            g.fillRoundedRectangle (bounds, cornerRadius);
        }

        const auto edge = on ? backgroundColour.withAlpha (0.75f) : border;
        g.setColour (edge);
        g.drawRoundedRectangle (bounds, cornerRadius, 1.0f);
        return;
    }

    // The default: a latching switch. Softer corner than a field -- see the
    // two radii in Metrics.h.
    auto face = raise;
    if (on)                                 face = face.brighter (kOnLift);
    if (shouldDrawButtonAsHighlighted)      face = face.brighter (kHoverLift);
    if (shouldDrawButtonAsDown)             face = face.darker   (0.10f);

    g.setColour (face);
    g.fillRoundedRectangle (bounds, switchRadius);

    g.setColour (on ? border.brighter (0.35f) : border);
    g.drawRoundedRectangle (bounds, switchRadius, 1.0f);

    if (! on)
        return;

    // The lamp: a lit bar across the top edge, and the light it spills onto
    // the face below it. backgroundColour is what JUCE resolved from
    // buttonOnColourId, so the caller's meaning arrives here for free.
    const auto lamp = backgroundColour;
    const auto top  = bounds.removeFromTop ((float) lampHeight);

    juce::Path bar;
    bar.addRoundedRectangle (top.getX(), top.getY(), top.getWidth(), top.getHeight(),
                             switchRadius, switchRadius, true, true, false, false);
    g.setColour (lamp);
    g.fillPath (bar);

    juce::ColourGradient spill (lamp.withAlpha (0.22f), top.getCentreX(), top.getBottom(),
                                lamp.withAlpha (0.0f),  top.getCentreX(), top.getBottom() + kLampGlowSpan,
                                false);
    g.setGradientFill (spill);
    g.fillRect (top.getX(), top.getBottom(), top.getWidth(), kLampGlowSpan);
}

void AzLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                    const bool shouldDrawButtonAsHighlighted, bool)
{
    const auto style = styleOf (button);
    const bool on    = button.getToggleState();

    auto colour = style == styleDanger
                      ? danger.withAlpha (shouldDrawButtonAsHighlighted ? 1.0f : 0.8f)
                      : button.findColour (on ? juce::TextButton::textColourOnId
                                              : juce::TextButton::textColourOffId);

    if (! button.isEnabled())
        colour = colour.withAlpha (0.4f);

    // Segments and chips are set in the NUMERIC face, in SENTENCE CASE, and
    // untracked: they sit directly beside the data they modify and must not
    // shout over it. Only switches and the destructive control are uppercase
    // tracked legends.
    const auto buttonStyle = styleOf (button);

    if (buttonStyle == styleSegment || buttonStyle == styleGhost)
    {
        g.setColour (button.findColour (button.getToggleState()
                                            ? juce::TextButton::textColourOnId
                                            : juce::TextButton::textColourOffId)
                         .withAlpha (button.isEnabled() ? 1.0f : 0.4f));
        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds(),
                          juce::Justification::centred, 1, 0.9f);
        return;
    }

    const auto hint = button.getProperties()
                            .getWithDefault (hintProperty, juce::String()).toString();

    auto area = button.getLocalBounds().reduced (gap, spacing);

    // A switch with a hint splits its face: legend on top, hint beneath. The
    // legend keeps the optical centre, so the pair does not read as having
    // slid upward -- the hint band is carved off the bottom AFTER the legend
    // has been given the middle.
    juce::Rectangle<int> hintArea;
    if (hint.isNotEmpty() && area.getHeight() >= 40)
        hintArea = area.removeFromBottom (16);

    g.setColour (colour);
    g.setFont (getTextButtonFont (button, button.getHeight()));

    // Legends are silkscreen: uppercase, tracked. The tracking already comes
    // from the font, so the only job here is the case and the fit.
    g.drawFittedText (button.getButtonText().toUpperCase(), area,
                      juce::Justification::centred, 1, kLegendMinScale);

    if (hintArea.isEmpty())
        return;

    g.setColour (faded.withAlpha (button.isEnabled() ? 1.0f : 0.4f));
    g.setFont (monoFont (10.0f));
    g.drawFittedText (hint, hintArea, juce::Justification::centred, 1, 0.8f);
}

juce::Font AzLookAndFeel::getTextButtonFont (juce::TextButton& button, const int buttonHeight)
{
    // Every size and tracking below is transcribed from the original design
    // study. They are NOT one scale: a segment is set in the numeric face and
    // a switch in the tracked legend face, and using one for both is what
    // made the toolbar shout over the data beside it.
    const auto style = styleOf (button);

    if (style == styleSegment)
        return monoFont (segmentFontSize);

    if (style == styleGhost)
        return monoFont (chipFontSize);

    if (style == styleDanger)
        return legendFont (dangerFontSize, true, trackingSwitch);

    // A transport switch, or a smaller default button in a strip. The 40 px
    // split is the boundary between "a switch" and "a control in a row".
    return buttonHeight >= 40 ? legendFont (switchFontSize, true, trackingSwitch)
                              : legendFont (columnFontSize, true, trackingColumn);
}

} // namespace az::ui
