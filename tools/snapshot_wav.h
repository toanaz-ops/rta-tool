// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The `main-wav.png` specimen (H11 W3).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rta::tools
{

// Renders the real MainComponent after LOAD WAV and ANALYSE on a file the tool writes itself
// (pink through a +6 dB peaking filter lagging 37 samples, ch3 against ch1 of a 4-channel
// 24-bit WAV): the rail shows the WAV row filled in with its readout, and the TRANSFER pane
// shows the stored WAV trace. Increments `failures` and prints a FAILED message if the
// analysis does not finish with a trace.
void renderWavSpecimen (const juce::File& outDir, int& failures);

} // namespace rta::tools
