// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Uses JUCE (juce::Graphics). Decision 3 of
// docs/dsp/2026-08-29-display-layer-l5c.md, drawing half. Split out of
// TransferView.cpp per the project's file-length cap: the ribbon is the one
// pane whose whole job -- border, caption, per-column fill -- has no other
// caller, so it is the seam that keeps the composite's own file under 300
// lines.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "view/BodeLayout.h"
#include "view/PlotGeometry.h"

#include <span>

namespace rta::view {

/// Draws the coherence ribbon for the LIVE capture only (decision 3): a
/// border and a "COH" caption -- furniture, drawn regardless of whether there
/// is data -- and, when `coherence` is non-empty, one filled pixel column per
/// column with alpha from the per-column MINIMUM gamma^2 (`columnAlpha`,
/// CoherenceAlpha.h), in the amber accent so the strip visually belongs to
/// the trace whose trust it reports.
///
/// `coherence` empty means "nothing was measured yet" (Snapshot.h:
/// `TransferBlock::coherence` is absent below the engine's effective-average
/// gate) -- NOT the same "no trust information, draw opaque" contract
/// TraceStroke.h documents for a magnitude/phase trace. A magnitude/phase
/// trace is real data whether or not its trust is known; the ribbon's entire
/// content IS the trust. Painting it solid amber when there is nothing to
/// report would assert a confidence that was never measured, so this
/// function draws the frame alone and leaves the strip blank instead.
///
/// `columnForBin` and `columnCount` come from the SAME shared frequency axis
/// every other pane in the composite uses (decision 1, BodeLayout.h) -- this
/// function never computes its own x mapping.
void drawTransferRibbon(juce::Graphics& g, const PlotGeometry& geometry, PaneRect ribbonArea,
                        std::span<const float> coherence, std::span<const int> columnForBin,
                        int columnCount);

}  // namespace rta::view
