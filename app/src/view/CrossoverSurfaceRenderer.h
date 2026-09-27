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
//
// The INVERSION-UNKNOWN warning chip (owner decision, 2026-09-27, PR #45 fix
// round 3) is the one exception, and stays IN here rather than with the
// caller: unlike the topology, `CrossoverSurface::targetAmbiguous()` IS a
// queryable property of the surface itself, so both the live pane and
// `PhaseAlignPreview.cpp` get the warning for free with no duplicated call
// site, the same reason the shared grid/series code was extracted here in
// the first place.
#pragma once

#include "view/CrossoverSurface.h"
#include "view/PlotGeometry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace rta::view {

/// The row(s), in degrees, at which one candidate target line should paint,
/// given its own expected-offset `degrees` (already wrapped to `(-180, 180]`
/// by `CrossoverTopology.cpp`) and how many degrees of margin a row needs to
/// clear the plot's own 1px frame (`insetDeg`, converted from a pixel
/// distance by the caller so it is a header-testable pure function with no
/// `Graphics` dependency). Normally one row; exactly two -- one just inside
/// each edge -- when the candidate itself sits at `+180` or `-180`, so it
/// cannot be mistaken for the frame (owner decision, 2026-09-27: the
/// verifier's specimen showed a 180 deg candidate drawn exactly ON the
/// frame and read as part of it).
[[nodiscard]] std::vector<double> targetLineRowsDeg(double degrees, double insetDeg);

/// The corner label for one candidate target line: "<degrees> deg  NOT
/// INVERTED" for the PRIMARY candidate (`CrossoverSurface::targetRadians()`,
/// always the `ProcessorInversion::No` value -- `CrossoverTopology.cpp`'s
/// `Unknown` branch sets `radians = raw`, the same expression as its `No`
/// case) or "<degrees> deg  INVERTED" for the alternative (always `Yes`,
/// same reasoning). `degrees` is read off the ACTUAL line, never assumed --
/// for an LR-2 pair the primary candidate is 180, not 0 (owner decision,
/// 2026-09-27: "the labels follow the value, not a fixed order"). Degrees
/// print as whole numbers with the word "deg", matching the one existing
/// precedent for this exact quantity (`PhaseAlignPreview.cpp`'s topology
/// chip), not the `deg` symbol -- one spelling for one quantity.
[[nodiscard]] juce::String targetLineLabel(double degrees, bool isPrimaryCandidate);

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
/// ASKED target line (dotted, at `surface.targetRadians()`) and the
/// `arg(H_A conj H_B)` curve itself. When `surface.targetAmbiguous()` --
/// `ProcessorInversion::Unknown`, wizard question (c) answered "don't know"
/// -- draws BOTH candidates instead of the single line: the primary
/// (`targetRadians()`) stays dotted, the alternative
/// (`alternativeTargetRadians()`) draws solid, each labelled at its right
/// end (`targetLineLabel`) and wrapped off the frame at a +-180 edge
/// (`targetLineRowsDeg`), plus the `INVERSION UNKNOWN` corner chip -- see
/// this file's own header comment for why the chip lives here and not with
/// the caller. Draws no other legend, no chip naming the topology: see this
/// file's header comment for why THAT stays with the caller.
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
