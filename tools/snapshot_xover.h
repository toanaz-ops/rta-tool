// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The `main-live-xover*.png` specimens -- split out of tools/snapshot.cpp
// (PR #45 fix round 3) to stay under the 400-line file cap once a second
// specimen (UNKNOWN inversion, owner decision 2026-09-27) joined the first.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rta::tools
{

// Renders the live XOVER pane twice: NOT INVERTED (the everyday case) and
// UNKNOWN (the two-candidate-line rendering, docs/dsp/2026-09-06-l7-
// alignment-wizard.md Sec.13 Q3). Increments `failures` and prints the same
// FAILED message tools/snapshot.cpp's other specimens use for any render
// that does not complete.
void renderXoverSpecimens (const juce::File& outDir, int& failures);

} // namespace rta::tools
