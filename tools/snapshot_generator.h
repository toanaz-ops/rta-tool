// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The `main-live-gen.png` specimen (H7 wave B1, task T9).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rta::tools
{

// Drives the REAL masthead generator against SYNTHETIC: SINE, 1000 Hz, the
// default -30.0 dBFS, out 1, started through the same handlers the GEN button,
// the SIGNAL and FREQ combos call; waits until the controller says it is
// playing AND the RTA's strongest bin sits within one bin of 1000 Hz, and
// renders the window. Increments `failures` and prints a FAILED message -- never
// a silent placeholder render -- if the generator is not playing or the tone
// never appears in the spectrum.
void renderGeneratorSpecimen (const juce::File& outDir, int& failures);

} // namespace rta::tools
