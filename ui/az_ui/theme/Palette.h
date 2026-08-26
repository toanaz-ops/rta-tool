// SPDX-License-Identifier: AGPL-3.0-or-later
// SODIUM RACK -- the palette half of the design system's one source of
// truth. Every colour a consuming app draws with should come from here; a
// hex literal anywhere else is a sign the token set needs another entry
// instead.
//
// The direction, and why it is this and not something else
// ========================================================
// This design system targets a front-of-house tool: read for about one
// second at a time, from six feet away, in a dark room, by someone whose
// hands are busy. So the vernacular is not "dark dashboard" -- it is the
// touring rack: powder-coated panels, silkscreened DIN legends, engraved
// grooves between sections, and illuminated latching switches big enough to
// hit without looking.
//
// The palette values are written as ARGB integers (0xAARRGGBB), not as
// string literals, so a grep for "#RRGGBB" across consuming code stays a
// reliable signal that a colour has leaked outside this token set.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace az::ui
{

//==============================================================================
// Palette.
//
// juce::Colour's constructor is not constexpr in this JUCE version, so the
// constexpr tokens are the raw ARGB integers; the juce::Colour objects below
// are the readable aliases every consumer uses.
//
// The neutrals are warm-neutral graphite, NOT blue-black. That is a
// deliberate move off the default: blue-black plus one bright accent is the
// look every dark audio tool already has, and next to real gear it reads
// cold and screeny.

inline constexpr juce::uint32 backgroundArgb = 0xff0a0b0du; // window / canvas
inline constexpr juce::uint32 panelArgb      = 0xff131519u; // rack panel face
inline constexpr juce::uint32 raiseArgb      = 0xff1b1e24u; // raised / hover cell
inline constexpr juce::uint32 wellArgb       = 0xff0c0e11u; // recessed field (combos)
inline constexpr juce::uint32 borderArgb     = 0xff2b2f37u; // engraved hairline
inline constexpr juce::uint32 shadeArgb      = 0xff060709u; // the DARK half of a groove
inline constexpr juce::uint32 textArgb       = 0xffe8eaedu; // silkscreen white
inline constexpr juce::uint32 dimArgb        = 0xff868d98u; // secondary silkscreen
inline constexpr juce::uint32 fadedArgb      = 0xff5c636eu; // tertiary: units, row numbers
inline constexpr juce::uint32 accentArgb     = 0xffff9f1cu; // sodium vapour -- THE accent
inline constexpr juce::uint32 warnArgb       = 0xffffc24du; // caution
inline constexpr juce::uint32 okArgb         = 0xff6ee7a0u; // LED SIG green: PROTECTING only
inline constexpr juce::uint32 dangerArgb     = 0xffff5a4eu; // destructive / clipping

inline const juce::Colour background { backgroundArgb };
inline const juce::Colour panel      { panelArgb };
inline const juce::Colour raise      { raiseArgb };
inline const juce::Colour well       { wellArgb };
inline const juce::Colour border     { borderArgb };
inline const juce::Colour shade      { shadeArgb };
inline const juce::Colour text       { textArgb };
inline const juce::Colour dim        { dimArgb };
inline const juce::Colour faded      { fadedArgb };
inline const juce::Colour accent     { accentArgb };
inline const juce::Colour warn       { warnArgb };
inline const juce::Colour ok         { okArgb };
inline const juce::Colour danger     { dangerArgb };

// The light half of an engraved groove. A groove is one dark line with one
// faint light line under it -- the bevel that reads as "milled into the
// panel" rather than "a box drawn on top of it".
inline const juce::Colour sheen = juce::Colours::white.withAlpha (0.045f);

} // namespace az::ui
