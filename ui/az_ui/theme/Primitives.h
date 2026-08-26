// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Palette.h"
#include "Metrics.h"
#include "Typography.h"

namespace az::ui
{

//==============================================================================
// Shared drawing primitives. These exist so "an engraved divider" or "a
// recessed well" is drawn identically everywhere instead of being
// re-invented per component -- and so the hex literals stay in this folder.

// The bevel under a band: a dark line with a faint light line beneath it.
void drawEngravedDivider (juce::Graphics& g, juce::Rectangle<int> band);

// A recessed field: what a combo box, a value readout or a text well sits in.
void drawWell (juce::Graphics& g, juce::Rectangle<float> bounds, bool focused);

// The component property a caller sets to give a switch its second line:
//     button.getProperties().set (az::ui::hintProperty, "sweep the room");
// Drawn under the legend, quiet and small. A switch legend says WHAT the mode
// is called; the hint says what it does, which is the part a first-time user
// who has not read a manual needs.
inline const juce::Identifier hintProperty { "azHint" };

// Width of `content` set in `font`. juce::Font lost getStringWidth in this JUCE
// version; GlyphArrangement is the replacement and this is the one wrapper.
[[nodiscard]] float stringWidth (const juce::Font& font, const juce::String& content);

// Shortens `content` to fit `maxWidth` by removing characters from the MIDDLE,
// never the end.
//
// A channel is called "Analogue 1", and the part that identifies it is the
// LAST character. Trailing truncation -- which is what every default does --
// throws away the only part that matters and leaves two ports looking
// identical. "Ana... 1" is readable; "Analogue" twice is not.
[[nodiscard]] juce::String elideMiddle (const juce::Font& font,
                                        const juce::String& content,
                                        float maxWidth);

// A section caption, in tracked uppercase silkscreen.
void drawCaption (juce::Graphics& g, const juce::String& caption,
                  juce::Rectangle<int> bounds, juce::Colour colour);

// Halos are NOT here on purpose. A glow is a real gaussian (melatonin_blur),
// and melatonin caches per shadow OBJECT -- so the object has to be a member
// of the component that draws it. A theme-level helper would construct one
// per call and throw the cache away every frame -- draw halos in the
// component itself.

} // namespace az::ui
