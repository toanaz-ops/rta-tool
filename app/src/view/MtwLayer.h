// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Uses JUCE (juce::Graphics), so it is NOT
// in the measure_has_no_framework_deps file list -- same reasoning as
// TraceStroke.h. Task 7 of docs/plans/2026-09-05-L3-mtw-impl-plan.md.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "measure/Snapshot.h"
#include "view/PlotGeometry.h"

#include <cstddef>
#include <vector>

namespace rta::view {

/// Per-stitched-point trust, ready to feed straight into `columnAlpha` (the
/// same function the fixed engine's own coherence uses).
///
/// A band that has not yet passed its own gate (`!band.coherenceAvailable`)
/// contributes 1.0 -- fully trusted -- to every one of its points, NOT the
/// coherence-alpha floor and NOT a NaN. This is the fixed engine's own
/// contract (TraceStroke.h: "an EMPTY span means this trace carries no
/// coherence... a different thing from coherence is zero") extended per
/// band instead of per whole block, because record §5 (conflict C3) makes
/// "still filling" a PER-BAND fact: the bottom band can still be filling
/// while the top has been measuring for seconds, and neither is a measured
/// distrust of the signal.
[[nodiscard]] std::vector<float> mtwColumnAlpha(const rta::measure::MtwBlock& block,
                                                const std::vector<int>& columns,
                                                std::size_t columnCount);

/// A hairline at `geometry.xForHz(band.seamHz)` for every band but the
/// bottom's (whose `seamHz` is 0 -- no lower neighbour, and `xForHz(0)` is
/// non-finite in any case). `seamColour` is supplied by the caller
/// (MeasureColours.h's `mtwSeam`) -- this file adds no colour token of its
/// own, the same rule TransferView.cpp's other JUCE-using neighbours follow.
void drawMtwSeams(juce::Graphics& g, const rta::measure::MtwBlock& block,
                  const PlotGeometry& geometry, juce::Colour seamColour);

}  // namespace rta::view
