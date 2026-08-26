// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AzLookAndFeel.h"

#include "Palette.h"

namespace az::ui
{

//==============================================================================
// The palette is installed as LookAndFeel COLOURS rather than painted ad hoc:
// components that only call findColour() pick it up without knowing about
// the theme, and the overrides in AzLookAndFeel_Buttons.cpp and
// AzLookAndFeel_Widgets.cpp handle the shapes -- switch lamps, engraved
// wells, hardware toggles -- that colour ids cannot express.

AzLookAndFeel::AzLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, background);

    // A switch face is `raise`; the lamp colour rides on buttonOnColourId so a
    // component can say what its ON state MEANS (green = protecting) without
    // knowing how the switch is drawn.
    setColour (juce::TextButton::buttonColourId,    raise);
    setColour (juce::TextButton::buttonOnColourId,  accent);
    setColour (juce::TextButton::textColourOffId,   dim);
    setColour (juce::TextButton::textColourOnId,    text);

    setColour (juce::Label::textColourId,           text);

    setColour (juce::ComboBox::backgroundColourId,  well);
    setColour (juce::ComboBox::outlineColourId,     border);
    setColour (juce::ComboBox::textColourId,        text);
    setColour (juce::ComboBox::arrowColourId,       faded);
    setColour (juce::ComboBox::buttonColourId,      well);
    setColour (juce::ComboBox::focusedOutlineColourId, accent);

    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId,       text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, raise);
    setColour (juce::PopupMenu::highlightedTextColourId,       accent);

    setColour (juce::ToggleButton::textColourId,    text);
    setColour (juce::ToggleButton::tickColourId,    accent);

    setColour (juce::ScrollBar::thumbColourId,      border);
    setColour (juce::ScrollBar::trackColourId,      background);

    setColour (juce::TooltipWindow::backgroundColourId, panel);
    setColour (juce::TooltipWindow::textColourId,       text);
    setColour (juce::TooltipWindow::outlineColourId,    border);

    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId,       text);
    setColour (juce::AlertWindow::outlineColourId,    border);
}

} // namespace az::ui
