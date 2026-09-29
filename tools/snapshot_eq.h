// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The `main-live-eq*.png` specimens (L7-EQ UI wave A, task T9, docs/plans/
// 2026-09-29-eq-ui-lane-plan.md "UI snapshot specimens"). Split out of
// tools/snapshot.cpp the way snapshot_xover.h is, to keep that file under the
// 400-line cap.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rta::tools
{

// Renders both specimens; increments `failures` and prints FAILED for any that
// does not complete (a picture of an empty or refusing pane would prove nothing).
//
//  main-live-eq.png (1280x800): the real MainComponent in SYNTHETIC mode, its
//  library seeded with the 8 dB bump trace (coherence 0.95), driven through the
//  real controls -- EQ button, measurement combo, AUTO EQ button. FAILED if
//  nothing is committed.
//
//  main-live-store-eq.png (1280x800): the real chain -- TRANSFER, the real STORE
//  button seam, EQ, pick the stored trace, SUGGEST. FAILED if no reference with
//  coherence is fed, the stored trace is not eligible as a measurement, or the
//  pane refuses or leaves its readout blank. SYNTHETIC H is nearly flat, so the
//  readout says "nothing to correct" or lists candidates -- never blank.
void renderEqSpecimens (const juce::File& outDir, int& failures);

} // namespace rta::tools
