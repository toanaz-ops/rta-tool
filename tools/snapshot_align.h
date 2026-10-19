// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The `main-live-align.png` specimen (ALIGN-UI lane wave A, task T7).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rta::tools
{

// Drives the REAL ALIGN pane against SYNTHETIC with the plan's fixture (an
// LR4 high-pass after 4 samples on output 0, an LR4 low-pass after 100 on
// output 1): answers the four questions through the pane's own combos, presses
// MEASURE, types the coarse offset, presses SUM, then renders
// "main-live-align.png". Increments `failures` and prints a FAILED message --
// never a silent placeholder render -- if any step does not complete.
void renderAlignSpecimen (const juce::File& outDir, int& failures);

} // namespace rta::tools
