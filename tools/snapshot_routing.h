// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The `main-live-routing16.png` specimen (lane H2, QA queue D1/D8) -- a
// companion file to tools/snapshot.cpp so that file stays under the 400-line
// cap, the same reason tools/snapshot_xover.h exists.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rta::tools
{

// Renders the real MainComponent as if a 16-input interface were open: the
// routing matrix carries 16 rows and is scrolled to its last rows, so rows
// past the old 8-row cap are what the picture shows, including a route on
// channel 12 (reference channel 0, transfer function 3). Names are pushed through
// MainComponentTestAccess::applyChannelNamesForTest -- the same function the
// device poll calls -- so no interface is needed. Increments `failures` and
// prints a FAILED message if the render does not complete.
void renderRoutingSpecimen (const juce::File& outDir, int& failures);

} // namespace rta::tools
