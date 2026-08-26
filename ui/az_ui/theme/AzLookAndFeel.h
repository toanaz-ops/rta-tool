// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace az::ui
{

//==============================================================================
// The LookAndFeel that installs the palette application-wide.
//
// The overrides are split across three translation units -- AzLookAndFeel.cpp
// (colour setup), AzLookAndFeel_Buttons.cpp (button drawing) and
// AzLookAndFeel_Widgets.cpp (combo box, toggle, label, popup menu, scrollbar)
// -- purely to keep any one file a reasonable size; the class itself is one
// unit as far as callers are concerned.

class AzLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AzLookAndFeel();

    // Buttons are switches: a raised cell, and when latched ON a lit bar
    // across the top edge plus a halo -- the state reads from across the room
    // rather than only from the label.
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    // A per-row enable in a settings table. Drawn as a hardware toggle -- a
    // track with a travelling knob -- because a JUCE tick box is both tiny
    // and ambiguous at arm's length.
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawLabel (juce::Graphics&, juce::Label&) override;
    juce::Font getLabelFont (juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    juce::Font getPopupMenuFont() override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;
};

} // namespace az::ui
