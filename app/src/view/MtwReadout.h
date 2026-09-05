// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Uses JUCE (juce::Graphics, juce::String),
// so it is NOT in the measure_has_no_framework_deps file list -- same
// reasoning as MtwLayer.h. Station-4 fix pass, lane L3b finding 1: record §5
// (docs/dsp/2026-09-05-mtw-l3.md) requires the view to show per-band
// `integrationSeconds`, "because a coherence trace that fills in from the
// top over five seconds is otherwise read as a fault" -- nothing drew it.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "measure/Snapshot.h"
#include "view/PlotGeometry.h"

#include <cstddef>

namespace rta::view {

/// One band's `integrationSeconds`, formatted per record §5's own purpose:
/// the operator must be able to tell, at a glance, that the low end fills
/// slowly. CLAUDE.md's "dB keeps one decimal" rule does not cover seconds --
/// there is no project rule for this quantity -- so the threshold is chosen
/// the same way that one was: one decimal above 1 s (0.1 s is the smallest
/// actionable difference once integration is already multiple seconds), two
/// decimals below 1 s (the two fastest bands are 0.09 s and 0.17 s; one
/// decimal would round both to "0.1 s" and erase the only distinction
/// between them).
[[nodiscard]] juce::String formatIntegrationSeconds(float seconds);

/// One band's frequency boundary, in whole hertz (CLAUDE.md "Reading out
/// numbers": frequency never carries a decimal). `bandIndex` indexes
/// `block.bands`, which is ascending in frequency (`Snapshot.h`'s own
/// comment on the field):
///   - the bottom band (`seamHz == 0`, no lower neighbour) reads
///     "< <next band's seamHz> Hz" -- the one entry that states the unit,
///     since it is the only entry that is not visibly a range;
///   - the top band (no next band) reads "> <this band's seamHz>";
///   - every band between reads "<this seamHz>-<next seamHz>" -- an ASCII
///     hyphen, not an en dash, so the string round-trips through any
///     narrower-than-UTF-8 rendering path untouched; the project's own
///     `/utf-8` MSVC flag makes either safe to compile, but there is no
///     reason to spend the wider glyph on furniture text.
/// A single-band block (no boundary to state) reads "ALL".
[[nodiscard]] juce::String formatBandRange(const rta::measure::MtwBlock& block, std::size_t bandIndex);

/// The full strip, one segment per band, joined by " | " -- e.g.
/// "< 188 Hz  5.5 s | 188-375  2.7 s | ... | > 6000  0.09 s". Empty for an
/// empty `block.bands` (never reached in practice: `Analyser::publish` only
/// populates `Snapshot::mtw` with a full band table or leaves it absent).
[[nodiscard]] juce::String mtwIntegrationStrip(const rta::measure::MtwBlock& block);

/// Draws the strip at the top-left of `geometry`'s own plot area, in the
/// mono face every number in this app uses (`az::ui::monoFont` --
/// Typography.h: "what every NUMBER... is drawn in") and the seam hairline's
/// own dim tone (`MeasureColours.h`'s `mtwReadout`, the same token
/// `mtwSeam` aliases: both are furniture describing the MTW engine's own
/// layout, not data). A no-op when `block.bands` is empty.
void drawMtwIntegrationStrip(juce::Graphics& g, const rta::measure::MtwBlock& block,
                             const PlotGeometry& geometry);

}  // namespace rta::view
