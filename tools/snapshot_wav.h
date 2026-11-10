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

// H12 T4: `main-wav-pair.png`. The rail after LOAD WAV + LOAD MEAS WAV on two one-channel float32
// files (the measurement 80 ppm fast and 3.2 s late, `bext` references on both) and ANALYSE:
// both file names on the file line, the readout with the clock and offset, and the stored pair
// trace in the TRANSFER pane. Same failure contract as renderWavSpecimen.
void renderWavPairSpecimen (const juce::File& outDir, int& failures);

} // namespace rta::tools
