// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Uses JUCE (juce::Graphics), so it is NOT in
// the measure_has_no_framework_deps file list. Decision 3 of
// docs/dsp/2026-08-29-display-layer-l5c.md, drawing half.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "view/PhaseDecimator.h"
#include "view/PlotGeometry.h"
#include "view/TraceDecimator.h"

#include <span>

namespace rta::view {

/// One filled 1 px column per extent.
///
/// `columnAlpha` may be EMPTY, which means "this trace carries no coherence"
/// and draws fully opaque -- a different thing from "coherence is zero". A
/// single-channel capture has no trust information to show, and dimming it
/// would assert a measurement that was never taken. When non-empty it must be
/// the same length as `extents`; a shorter span draws the remainder opaque
/// rather than reading past its end.
void strokeMagnitudeExtents(juce::Graphics& g, std::span<const ColumnExtent> extents,
                            const PlotGeometry& geometry, int originY,
                            std::span<const float> columnAlpha, juce::Colour base);

/// Same contract, for wrapped phase.
///
/// Three shapes, one per DrawnPhaseColumn state: a plain extent, a straddle
/// (two pieces, at the top and bottom edges of the pane, with NOTHING between
/// them -- a line across the middle is the wrap artefact, not the curve), and
/// a full-height band where phase rotates faster than the column can resolve.
void strokePhaseColumns(juce::Graphics& g, std::span<const DrawnPhaseColumn> columns,
                        const PlotGeometry& geometry, int originY,
                        std::span<const float> columnAlpha, juce::Colour base);

}  // namespace rta::view
