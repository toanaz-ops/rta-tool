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
#include "dev/preview/SplPreview.h"
#include "dev/preview/TargetMatchPreview.h"
#include "dev/preview/TransferFunctionPreview.h"
#include "measure/Snapshot.h"
#include "measure/SnapshotSource.h"
#include "measure/SyntheticSnapshot.h"
#include "trace/Workspace.h"
#include "view/PaneRegistry.h"
#include "view/RtaView.h"
#include "view/TransferView.h"
#include "view/WorkspaceView.h"

#include <memory>
#include <vector>

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
        // transfer.png: the Bode composite (task 8), fed a deterministic
        // synthetic transfer (fixed fftSize/sampleRate/delay -- no threads,
        // no timing) so this is the same picture every run, the same
        // precondition rta-view.png already relies on. delaySamples=18 is
        // picked to review, not derived from a spec: at fftSize=4096 it
        // carries the phase trace through 7.5 wraps by 20 kHz (360*20000*18/
        // 48000 = 2700 degrees) -- enough to show the wrap/pen-lift machinery
        // repeatedly without collapsing into solid full-band columns -- and
        // both of makeSyntheticTransfer's coherence dips (LF, and the 2 kHz
        // notch) land inside the plotted 20-20000 Hz range. Changing either
        // number changes what the eyes-only review below is judging.
        auto snapshot = std::make_shared<rta::measure::Snapshot> ();
        snapshot->sequence = 1;
        snapshot->sampleRate = 48000.0;
        snapshot->fftSize = 4096;
        snapshot->transfer = rta::measure::makeSyntheticTransfer (snapshot->fftSize, snapshot->sampleRate, 18);
        // The MTW half of the picture (docs/reports/005-mtw-engine.md "Known
        // gaps": "no committed snapshot fixture carries an MtwBlock") -- the
        // default MtwConfig, so the seams this fixture draws land at the
        // record's own frequencies (187.5/375/750/1500/3000/6000 Hz) and the
        // per-band integration-seconds strip has real numbers to print, not a
        // temporary patch applied and reverted by hand each time someone
        // wants to look.
        snapshot->mtw = rta::measure::makeSyntheticMtw ();
        // Task B7: the spatial-average trace and the four-position summary
        // list, on the SAME fixed grid as `snapshot->transfer` above (see
        // makeSyntheticAverage's own comment for why it is that grid and
        // not the MTW one).
        {
            auto [average, positions] = rta::measure::makeSyntheticAverage (
                snapshot->fftSize, snapshot->sampleRate, 4);
            snapshot->average = std::move (average);
            snapshot->positions = std::move (positions);
        }
        const rta::measure::StaticSnapshotSource source (snapshot);

        rta::view::TransferView component (source);
        if (! renderComponent (component, outDir, "transfer.png", width, height))
            ++failures;
    }

    {
        // workspace.png: the 1..3 pane vertical stack (record decision 6) --
        // an `rta` pane above a `transfer` composite, both reading the SAME
        // snapshot, so the picture is one coherent (if synthetic)
        // measurement rather than two unrelated fixtures sharing a window.
        // Bands come from makeSyntheticSnapshot (same fixture rta-view.png
        // renders), transfer from makeSyntheticTransfer (same fixture
        // transfer.png renders) -- both deterministic, so this is the same
        // picture every run, same precondition as every other snapshot here.
        const rta::measure::SyntheticSpec spec;
        auto snapshot = std::make_shared<rta::measure::Snapshot> (*rta::measure::makeSyntheticSnapshot (spec));
        snapshot->transfer = rta::measure::makeSyntheticTransfer (snapshot->fftSize, snapshot->sampleRate, 18);
        // Same MTW fixture transfer.png carries -- see that block's comment.
        snapshot->mtw = rta::measure::makeSyntheticMtw ();
        // Same average fixture transfer.png carries -- see that block's comment.
        {
            auto [average, positions] = rta::measure::makeSyntheticAverage (
                snapshot->fftSize, snapshot->sampleRate, 4);
            snapshot->average = std::move (average);
            snapshot->positions = std::move (positions);
        }
        const rta::measure::StaticSnapshotSource source (snapshot);

        rta::view::WorkspaceView component (
            std::vector<rta::trace::PaneSpec>{ { "rta", 1.0f }, { "transfer", 1.0f } },
            [&source] (rta::view::PaneView view) -> std::unique_ptr<juce::Component>
            {
                if (view == rta::view::PaneView::Transfer)
                    return std::make_unique<rta::view::TransferView> (source);
                return std::make_unique<rta::view::RtaView> (source);
            });
        if (! renderComponent (component, outDir, "workspace.png", width, height))
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

    {
        // preview-spl.png: lane L6a task W2-D4 -- the SPL strip over canned
        // data (SplPreview.h's own comment). Same paint-only, no-timer
        // contract as the three previews above.
        SplPreview component;
        if (! renderComponent (component, outDir, "preview-spl.png", width, height))
            ++failures;
    }

    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);

    // A plain return is the proof that az_ui's teardown bug stays fixed: the
    // Typography cache is now deliberately leaked (see Typography.cpp), so
    // full static destruction no longer touches JUCE after juceInit unwinds.
    // If this exit ever corrupts the heap again, that fix regressed.
    return failures == 0 ? 0 : 1;
}
