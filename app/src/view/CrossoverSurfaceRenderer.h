// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view.
//
// The G18 chart drawing, extracted (app/crossover-pane, the ALIGN-R8
// reversal) from app/src/dev/preview/PhaseAlignPreview.cpp so the live XOVER
// pane (CrossoverPaneView.cpp) and the dev-preview specimen
// (PhaseAlignPreview.cpp, still driving rtatool_snapshot's preview-phase.png)
// draw the IDENTICAL grid/series/target-line code rather than two copies a
// later change could quietly let drift apart.
//
// What stayed OUT of here, on purpose: the corner chip naming the asked
// topology and order ("BW4  ASKED TARGET 360 deg"), and the masthead.
// `CrossoverSurface` (view/CrossoverSurface.h) does not expose the
// `Topology`/`ProcessorInversion` it was given back out as a getter -- by
// design, per that header's own comment, topology is asked, never re-derived
// -- so only the CALLER, which is the one that called `setAskedTopology` in
// the first place, can write that legend. `crossoverPhaseGeometry`/
// `crossoverSummationGeometry` are exported precisely so a caller's own chip
// text lands in the same coordinate frame the grid below it was drawn in,
// with no second geometry construction to drift out of step.
#pragma once

#include "view/CrossoverSurface.h"
#include "view/PlotGeometry.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace rta::view {

/// The relative-phase pane's plot rectangle: degrees, +180 at the top, -180
/// at the bottom, over the log-frequency axis `paintCrossoverPhase` also
/// uses internally.
[[nodiscard]] PlotGeometry crossoverPhaseGeometry(juce::Rectangle<int> area);

/// The summed-magnitude pane's plot rectangle: +12 dB at the top, -24 dB at
/// the bottom -- headroom above `SurfaceMarks::coherentSumDb` (+6.02 dB) and
/// well below a cancellation notch, the same range PhaseAlignPreview always
/// rendered at.
[[nodiscard]] PlotGeometry crossoverSummationGeometry(juce::Rectangle<int> area);

/// Grid, frequency/degree labels, the shaded fit-window band, the horizontal
/// ASKED target line (dashed, at `surface.targetRadians()`) and the
/// `arg(H_A conj H_B)` curve itself. When `surface.targetAmbiguous()` --
/// `ProcessorInversion::Unknown`, wizard question (c) answered "don't know"
/// -- draws a SECOND dashed line at `surface.alternativeTargetRadians()`
/// (record Sec.13.3) rather than picking one of the two candidates for the
/// operator. Draws nothing else -- no legend, no chip: see this file's
/// header comment for why that stays with the caller.
void paintCrossoverPhase(juce::Graphics& g, juce::Rectangle<int> area,
                         const CrossoverSurface& surface);

/// Grid, frequency/level labels, the two reference marks
/// (`SurfaceMarks::coherentSumDb`/`designedSumDb`) with their dB labels, and
/// the four series: `H_A`, `H_B`, the pre-alignment ghost sum (dashed) and
/// the predicted sum (solid) -- plus the one caption line naming which is
/// which, since that line is equally true for the live pane and the preview.
void paintCrossoverSummation(juce::Graphics& g, juce::Rectangle<int> area,
                             const CrossoverSurface& surface);

}  // namespace rta::view
