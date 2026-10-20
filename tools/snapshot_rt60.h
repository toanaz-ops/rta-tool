// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The `main-live-rt60.png` specimen (RT60-UI lane wave A, task T7).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rta::tools
{

// Drives the REAL RT60 pane against SYNTHETIC with the comb "room" (a feedback
// comb in output 0's path, T60 = 0.80 s in every band; reachable only through the
// test hook, the shipped SYNTHETIC path has no room): presses the pane's own
// SWEEP button, waits for the real sweep and the analysis, and renders the pane
// with its table. Increments `failures` and prints a FAILED message -- never a
// silent placeholder render -- if any step does not complete or no octave from
// 125 Hz up reports a T30.
void renderRt60Specimen (const juce::File& outDir, int& failures);

} // namespace rta::tools
