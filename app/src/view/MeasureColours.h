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

/// The target/tuning judgement grammar (docs/specs/2026-08-28-interactive-
/// tuning-visuals.md, "The principle all three views share"), added for the
/// L5 preview mockups (app/src/dev/preview/) and reused as-is once the real
/// views land. Every name below is still an alias to an existing `az::ui`
/// token -- some of them the SAME token an existing name above already
/// aliases, because the judgement grammar and the RTA view's own vocabulary
/// occasionally mean the same colour for a different reason (`untrusted` is
/// `underResolved`'s tone: both say "refuse to judge", one because the FFT
/// cannot resolve the band, the other because coherence says the read is
/// noise). Duplicating the alias rather than reusing the RTA-view name keeps
/// each call site's grep-for-meaning honest about WHY a bar is dim.

/// A band or segment inside its tolerance corridor AND trusted -- LED SIG
/// green, per the spec's "match = az::ui `ok`". Never used for anything else
/// in this app (SODIUM RACK rule: ice blue, not this token, is the one
/// colour reserved for a single meaning, but a green used for two different
/// verdicts would erode the same way).
inline const juce::Colour match = az::ui::ok;

/// Outside the tolerance corridor, ranked among the worst offenders --
/// destructive red, the same token `az::ui::danger` already means
/// "clipping" elsewhere in the app. A miss is not a fault, but it is the
/// thing on screen the engineer most needs their eye pulled to.
inline const juce::Colour miss = az::ui::danger;

/// Low coherence or an under-resolved band: judged by NOBODY, drawn dimmed
/// and hatched (`drawHatch` in PreviewFurniture.h) rather than tinted alone
/// -- same reasoning `RtaView.cpp` already states for `underResolved`, "a
/// dimmed bar in a dark room is a dimmed bar nobody notices".
inline const juce::Colour untrusted = az::ui::faded;

/// A borderline read: not a clean match, not yet a ranked miss -- e.g. a
/// match-score chip's middle tier. `az::ui::warn` already carries this
/// exact meaning for the device panel's drop counter (DevicePanel.cpp:
/// "warn -- not danger -- a drop is a symptom the callback survived, not a
/// fault"); a judgement grammar with only two colours either over-praises a
/// mediocre score as green or over-alarms it as red.
inline const juce::Colour borderline = az::ui::warn;

/// The target curve / reference line -- a neutral secondary silkscreen tone
/// so it reads as "the goal", not as data fighting the measured trace
/// (`trace`, sodium amber) for the eye's attention.
inline const juce::Colour target = az::ui::dim;

/// The second live trace in a two-source view (V2's two phase captures).
/// Full silkscreen white rather than another accent: exactly two traces are
/// ever on screen in that view, so contrast against `trace`'s amber is
/// enough to tell them apart without spending a second colour meaning.
inline const juce::Colour secondaryTrace = az::ui::text;

/// A stored trace recalled from the library -- history, not the live read.
/// Secondary silkscreen, and NOT `trace`'s amber: after an afternoon of tuning
/// a plot can carry a dozen recalled captures, and the one curve the engineer
/// must be able to find without thinking is the one measuring right now. The
/// accent stays spent on that. Individual captures are separated by
/// multiplying the brightness of this one tone (the library's `shadeIndex`
/// field, applied in StoredTraceLayer.cpp) rather than by inventing more
/// colours, which is what keeps the palette a design system instead of a bag
/// of hues.
inline const juce::Colour storedTrace = az::ui::dim;

/// A predicted-not-yet-real curve: the freeze-ghost pre-move trace (V1) and
/// the summation ghost (V2) both draw with this tone, always at reduced
/// alpha and dashed at the call site -- never solid, because solid-and-dim
/// reads as "quiet data" and a ghost is not data at all yet.
inline const juce::Colour ghost = az::ui::dim;

}  // namespace rta::view
