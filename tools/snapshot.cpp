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
//
// HISTORY: this tool used to crash with STATUS_HEAP_CORRUPTION on exit —
// az_ui's Typography kept its Typeface::Ptr cache in a function-local static
// whose destructor ran AFTER ScopedJuceInitialiser_GUI tore JUCE down. The
// cache is now deliberately leaked (see ui/az_ui/theme/Typography.cpp), so
// this tool exits with a plain return. If the crash ever returns, that fix
// regressed.

#include <cstdlib>

#include <juce_gui_extra/juce_gui_extra.h>

#include <az_ui/az_ui.h>

#include "AppTypefaces.h"
#include "MainComponent.h"
#include "dev/SpecimenComponent.h"
#include "dev/preview/PhaseAlignPreview.h"
#include "dev/preview/TargetMatchPreview.h"
#include "dev/preview/TransferFunctionPreview.h"
#include "measure/SnapshotSource.h"
#include "measure/SyntheticSnapshot.h"
#include "view/RtaView.h"

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

// Renders `component` to `outDir/fileName` at `width x height` and reports the
// result on stdout in the one format both snapshots share. Shared by
// SpecimenComponent and RtaView so a future third snapshot costs one call, not
// a second copy of the resize/paint/write dance.
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
        SpecimenComponent component;
        if (! renderComponent (component, outDir, "specimen.png", width, height))
            ++failures;
    }

    {
        // Fixed spec, fixed seed (SyntheticSpec's default: pink noise, seed
        // 0x5EED, 4 seconds) -- no threads, no timing, so this is the same
        // Snapshot every run on this machine, which is the precondition for
        // rta-view.png being reviewable as a byte-for-byte diff (plan §1.4,
        // §5.6, trap T-7).
        const rta::measure::SyntheticSpec spec;
        const rta::measure::StaticSnapshotSource source (rta::measure::makeSyntheticSnapshot (spec));

        rta::view::RtaView component (source);
        if (! renderComponent (component, outDir, "rta-view.png", width, height))
            ++failures;
    }

    {
        // main-live.png: the real measurement window (MainComponent), driven
        // through the exact same seam a user's SYNTHETIC switch click uses --
        // not a second, divergent path -- so the picture needs no interface
        // plugged in at all (plan §0 item 4, made literal for the harness
        // that verifies this deliverable). AnalysisThread needs real wall
        // time to drain a few hops of synthetic pink noise and publish past
        // the empty "NO SIGNAL" state before there is anything worth
        // rendering; Thread::sleep is acceptable HERE specifically because
        // this is the offscreen tool, not the real-time audio callback
        // (project CLAUDE.md's real-time-safety section governs app/'s
        // audio path, not this harness). Fixed at 1280 x 800 regardless of
        // the tool's own width/height arguments -- this picture needs room
        // for the rail beside the plot that specimen.png and rta-view.png
        // do not carry.
        MainComponent component;
        component.setSyntheticMode (true);
        juce::Thread::sleep (800);
        if (! renderComponent (component, outDir, "main-live.png", 1280, 800))
            ++failures;
    }

    // The three lane-L5 preview mockups (docs/specs/2026-08-28-interactive-
    // tuning-visuals.md): paint-only components fed canned synthetic data,
    // so -- like SpecimenComponent and RtaView above -- no message loop and
    // no timer ever has to fire for the picture to be complete.
    {
        TransferFunctionPreview component;
        if (! renderComponent (component, outDir, "preview-tf.png", width, height))
            ++failures;
    }

    {
        TargetMatchPreview component;
        if (! renderComponent (component, outDir, "preview-target.png", width, height))
            ++failures;
    }

    {
        PhaseAlignPreview component;
        if (! renderComponent (component, outDir, "preview-phase.png", width, height))
            ++failures;
    }

    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);

    // A plain return is the proof that az_ui's teardown bug stays fixed: the
    // Typography cache is now deliberately leaked (see Typography.cpp), so
    // full static destruction no longer touches JUCE after juceInit unwinds.
    // If this exit ever corrupts the heap again, that fix regressed.
    return failures == 0 ? 0 : 1;
}
