// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/dev/preview. See
// docs/specs/2026-08-28-interactive-tuning-visuals.md.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <az_ui/az_ui.h>

/// Drawing pieces shared by the three lane-L5 preview components so each one
/// is "target + live + judgement" (the design record's own words) rather
/// than three independent sketches that happen to sit in the same folder.
/// Everything here draws with `az::ui` primitives/tokens and
/// `rta::view::MeasureColours` names only -- no hex literal, same rule
/// `MeasureColours.h` states for itself.
namespace rta::dev::preview {

/// The two-piece masthead `SpecimenComponent` established
/// (app/src/dev/SpecimenComponent.cpp): a bold brand word, a lighter
/// qualifier, an engraved rule underneath. Reused here rather than
/// re-invented so a marketing screenshot of any of the three previews reads
/// as the same product as the specimen sheet and the real RTA view.
void drawMasthead(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                   const juce::String& subtitle);

/// A recessed readout chip: a dim, tracked-uppercase legend followed by a
/// mono value in `valueColour` -- the one judgement-grammar colour a chip
/// carries (`az::ui::ok` for a clean read, `rta::view::miss` for a bad one,
/// `rta::view::readoutText` for a chip that is just reporting a number with
/// no verdict attached, e.g. a delay reading). Background is
/// `az::ui::drawWell`, the same recessed field a combo box sits in --
/// deliberately, so a chip reads as "an instrument readout", not a button.
/// @param valueHeight  the value line's type size. Defaults to chip text; a
///                     score chip passes the countdown size instead, because a
///                     score's NUMBER is the content and the legend is only its
///                     address -- a big box around a small figure reads as an
///                     empty field with a caption.
void drawChip(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& legend,
              const juce::String& value, juce::Colour valueColour,
              float valueHeight = az::ui::chipFontSize);

/// Diagonal hairline hatch filling `area` -- the "judged by NOBODY" texture
/// the design record calls for wherever coherence is too low or a band is
/// under-resolved (docs/specs/2026-08-28-interactive-tuning-visuals.md, "The
/// principle all three views share"). A dimmed fill alone reads as "quiet
/// data"; the hatch is what makes "this region was refused, not measured
/// clean" legible from six feet in a dark room.
void drawHatch(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour);

}  // namespace rta::dev::preview
