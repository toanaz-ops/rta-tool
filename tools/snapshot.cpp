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
// PRE-EXISTING BUG, discovered while adding this tool's third render
// (main-live.png), documented here rather than silently patched around:
// az::ui::Typography.cpp's setTypefaces() lazily builds a function-local
// static (`faces()`) holding `juce::Typeface::Ptr` objects. A function-local
// static outlives every automatic (stack) variable in main(), including
// `juceInit` below -- so that static's own destructor runs AFTER
// ScopedJuceInitialiser_GUI has already torn down JUCE's font cache and
// graphics singletons, which corrupts the heap on every run of THIS tool
// (reproduces identically on an unmodified checkout: all PNGs still get
// written correctly first, then the process crashes on the way out). Fixing
// Typography.cpp itself is out of this task's touch list
// (docs/plans/2026-08-27-audioio-rta-impl-plan.md's T10 scope is
// MainComponent/Main.cpp/CMakeLists/this file only), so main() below calls
// std::quick_exit() instead of returning -- skipping static/global
// destruction entirely, which is safe for a one-shot CLI tool that has
// already finished writing every file it came here to write. The proper fix
// (a `juce::DeletedAtShutdown`-based cache, or an explicit
// `az::ui::shutdownTypefaces()` called before `juceInit` goes out of scope)
// belongs to az_ui and should land as its own change.

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

    // std::quick_exit, not return -- see the file-header comment. Every file
    // this tool exists to write is already flushed to disk at this point;
    // quick_exit terminates immediately, before `lookAndFeel` and `juceInit`
    // unwind and before the corrupting static destructor runs.
    std::fflush (nullptr);
    std::quick_exit (failures == 0 ? 0 : 1);
}
