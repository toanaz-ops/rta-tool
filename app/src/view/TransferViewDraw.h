// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Split out of TransferView.cpp (task B0,
// docs/plans/2026-09-06-L6b-impl-plan.md) to keep that file under the
// project's line cap before L6b adds to it -- no behaviour change.
#pragma once

#include "measure/Snapshot.h"
#include "view/BodeLayout.h"
#include "view/PlotGeometry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace rta::view {

/// Whole-degree axis labels, one per 90 deg -- copied from
/// TransferFunctionPreview::drawPhaseLabels.
void drawPhaseLabels(juce::Graphics& g, const PlotGeometry& geometry);

void drawNoReferenceState(juce::Graphics& g, PaneRect area);

/// Record §5a: hiding stored phase traces while unwrapped must never be
/// SILENT.
void drawStoredPhaseHiddenState(juce::Graphics& g, const PlotGeometry& geometry);

/// The live phase, continuous (no wrap), and the whole-multiple-of-360 axis
/// that encloses it (decision 4). Empty `unwrappedDeg` means the transfer
/// carried no phase to unwrap.
struct UnwrappedPhase {
    std::vector<float> unwrappedDeg;
    // The default WRAPPED phase axis (TransferView.cpp's own
    // kWrappedPhaseDbTop/kWrappedPhaseDbBottom, decision 4) -- duplicated as
    // a literal here rather than shared, because those constants stay in
    // TransferView.cpp's own anonymous namespace (still used at every
    // renderTo() call site there) and this header must not reach back into a
    // .cpp's internal namespace to read them.
    double dbTop = 180.0;
    double dbBottom = -180.0;
};

[[nodiscard]] UnwrappedPhase computeUnwrappedPhase(const rta::measure::TransferBlock& transfer);

}  // namespace rta::view
