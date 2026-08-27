// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.5, trap T-10.
#pragma once

#include <az_ui/az_ui.h>

/// Measurement-semantic colour names, each bound to exactly one `az::ui`
/// token. This file is the ONE place in the app allowed to say "trace" or
/// "under-resolved" next to a colour -- `ui/az_ui/` supplies the palette,
/// metrics, typography and primitives only, with no measurement vocabulary
/// of its own (project CLAUDE.md, "az_ui must contain no measurement
/// vocabulary"; plan trap T-10). The moment a colour named `coherence` or
/// `trace` appears inside `az_ui/theme/Palette.h`, that module stops being
/// a reusable design system and becomes this app's GUI in a different
/// folder.
///
/// Every name here is an alias, not a new colour: no hex literal, no
/// `juce::Colour` constructed from scratch. A grep for `0x` in this file
/// finding nothing is itself the check that the rule held.
namespace rta::view {

/// The band bars themselves -- the one trace this view draws. Sodium amber,
/// the design system's single accent.
inline const juce::Colour trace = az::ui::accent;

/// A band the FFT physically cannot resolve at the configured size (plan
/// §3.5.1: `BandWeights::kMinBinsPerBand`). Used for the 1 px outline those
/// bars get instead of a fill, and for the "RESOLUTION LIMIT" legend text --
/// never as a tint on an otherwise-normal bar, because a dimmed bar in a
/// dark room is a dimmed bar nobody notices.
inline const juce::Colour underResolved = az::ui::faded;

/// Hairline grid: decade ticks and dB gridlines. The same engraved-hairline
/// value every other panel in the app uses for a groove.
inline const juce::Colour grid = az::ui::border;

/// Frequency and dB axis label text -- secondary silkscreen, not the
/// brighter `text` token, so the numbers read as scale rather than data.
inline const juce::Colour axisText = az::ui::dim;

/// The readout line (selected band centre + level) and the empty-state
/// message both read at full silkscreen brightness: they are the one line
/// of prose this view ever puts in front of the plot itself.
inline const juce::Colour readoutText = az::ui::text;
inline const juce::Colour emptyStateText = az::ui::faded;

}  // namespace rta::view
