// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The `ir-view.png` specimen (H9 / L4c task A3).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rta::tools
{

// Renders the REAL IR-gate pane (through MainComponent, so the selector row and
// the rail are in the picture) over a synthetic impulse response: a direct sound,
// three discrete reflections and a decaying diffuse tail, with the default gate
// placed so the 12.5 ms reflection falls outside it. The IR is a fixture handed
// to the SWEEP half's IR store, not a measurement. Increments `failures` and
// prints a FAILED message if the pane does not build or the gate is refused.
void renderIrSpecimen (const juce::File& outDir, int& failures);

} // namespace rta::tools
