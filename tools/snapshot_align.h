// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The `main-live-align.png` specimen (ALIGN-UI lane wave A, task T7).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rta::tools
{

// Drives the REAL ALIGN pane against SYNTHETIC: answers the four questions
// through the pane's own combos, presses MEASURE, types the coarse offset,
// presses SUM, then renders it twice -- "main-live-align.png" with the SHIPPED
// SYNTHETIC defaults (what an operator sees) and "main-live-align-lr4.png" with
// the plan's LR4 high-pass / low-pass fixture (what the T6 bounds are derived
// from; reachable only through the test hook), and "main-live-align-ir.png"
// (wave B): the same fixture with SWEEP MAIN and SWEEP SUB pressed first, so the
// verdict's integer comes from the bound IR arrivals and no offset is typed.
// Increments `failures` and
// prints a FAILED message -- never a silent placeholder render -- if any step
// does not complete.
void renderAlignSpecimen (const juce::File& outDir, int& failures);

} // namespace rta::tools
