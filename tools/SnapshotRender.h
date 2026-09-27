// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The offscreen resize/paint/write dance every `rtatool_snapshot` specimen
// shares -- split out of tools/snapshot.cpp (PR #45 fix round 3) so
// tools/snapshot_xover.cpp can call the SAME function rather than keeping a
// second copy that could drift. See tools/snapshot.cpp's own header comment
// for why an offscreen render beats a screen capture.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rta::tools
{

// Renders `component` to `outDir/fileName` at `width x height` and reports
// the result on stdout in the one format every snapshot shares.
bool renderComponent (juce::Component& component, const juce::File& outDir,
                      const char* fileName, int width, int height);

} // namespace rta::tools
