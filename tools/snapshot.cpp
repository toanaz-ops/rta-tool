// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Renders the app's components to PNG offscreen -- no window, no desktop peer,
// no screen capture.
//
// Screen capture of the running app fails for reasons that have nothing to do
// with the app: another window drifts in front of the shot, desktop DPI scaling
// silently rescales it, layout has not settled after a programmatic resize, and
// the running binary holds a lock on its own .exe so the next build cannot
// link. A snapshot has none of those failure modes and is identical every run.
//
//   rtatool_snapshot [outputDir] [width] [height]

#include <juce_gui_extra/juce_gui_extra.h>

#include <az_ui/az_ui.h>

#include "AppTypefaces.h"
#include "MainComponent.h"

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

int main (int argc, char** argv)
{
    // The MessageManager has to exist before any Component is constructed.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File outDir = juce::File::getCurrentWorkingDirectory()
                                  .getChildFile (argc > 1 ? argv[1] : "shots");
    const int width  = argc > 2 ? juce::String (argv[2]).getIntValue() : 1100;
    const int height = argc > 3 ? juce::String (argv[3]).getIntValue() : 760;

    outDir.createDirectory();

    installAppTypefaces();
    az::ui::AzLookAndFeel lookAndFeel;
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    int failures = 0;
    {
        MainComponent component;
        component.setSize (width, height);

        // setSize() alone does not call resized() on a component with no
        // desktop peer, so children keep whatever bounds they had -- on a fresh
        // one, none at all, and the image comes out empty. This call is the
        // single most common thing missing from a blank snapshot.
        component.resized();

        // `true` renders children too; without it a container yields only its
        // own background.
        const auto image = component.createComponentSnapshot (component.getLocalBounds(), true);
        const auto file  = outDir.getChildFile ("specimen.png");

        if (writePng (image, file))
            std::printf ("wrote %s  (%d x %d)\n",
                         file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
        else
            (void) ++failures, std::printf ("FAILED to write %s\n", file.getFullPathName().toRawUTF8());
    }

    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    return failures == 0 ? 0 : 1;
}
