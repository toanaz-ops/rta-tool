// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of tools/. See SnapshotRender.h.
#include "SnapshotRender.h"

#include <cstdio>

namespace rta::tools
{
namespace
{

bool writePng (const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream stream (file);

    if (! stream.openedOk())
        return false;

    return juce::PNGImageFormat().writeImageToStream (image, stream);
}

} // namespace

bool renderComponent (juce::Component& component, const juce::File& outDir,
                      const char* fileName, int width, int height)
{
    component.setSize (width, height);

    // setSize() alone does not call resized() on a component with no desktop
    // peer, so children keep whatever bounds they had -- on a fresh one, none
    // at all, and the image comes out empty. This call is the single most
    // common thing missing from a blank snapshot (plan trap T-6).
    component.resized();

    // `true` renders children too; without it a container yields only its own
    // background (the other half of trap T-6).
    const auto image = component.createComponentSnapshot (component.getLocalBounds(), true);
    const auto file  = outDir.getChildFile (fileName);

    if (! writePng (image, file))
    {
        std::printf ("FAILED to write %s\n", file.getFullPathName().toRawUTF8());
        return false;
    }

    std::printf ("wrote %s  (%d x %d)\n",
                 file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    return true;
}

} // namespace rta::tools
