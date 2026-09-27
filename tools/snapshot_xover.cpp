// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of tools/. See snapshot_xover.h.
#include "snapshot_xover.h"

#include "SnapshotRender.h"

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "trace/Trace.h"
#include "trace/TraceLibrary.h"
#include "view/CrossoverPaneView.h"
#include "view/PaneRegistry.h"

#include <cstdio>
#include <utility>
#include <vector>

namespace rta::tools
{
namespace
{

// Flat magnitude, zero phase: sits on the LR-4 line asked below.
rta::trace::Trace makeXoverTrace (const char* id, float magnitudeDb)
{
    rta::trace::CaptureMeta meta;
    meta.id = id;
    meta.sampleRate = 48000.0;
    meta.fftSize = 2048;
    const auto points = rta::trace::pointCountFor (meta.fftSize);
    auto trace = rta::trace::Trace::make (meta, std::vector<float> (points, magnitudeDb));
    [[maybe_unused]] const bool phaseSet = trace->setPhase (std::vector<float> (points, 0.0f));
    jassert (phaseSet);
    return std::move (*trace);
}

// ALIGN-R8's live pane, PR #45 -- drives all four real ComboBox widgets an
// operator would, via MainComponentTestAccess.
void renderOneXoverSpecimen (const juce::File& outDir, int inversionItemId,
                             const char* fileName, int& failures)
{
    MainComponent component;
    component.setSyntheticMode (true);
    juce::Thread::sleep (800);

    auto& library = MainComponentTestAccess::libraryForTest (component);
    library.add (makeXoverTrace ("main-hp", -3.0f), "Main HP", "default");
    library.add (makeXoverTrace ("sub-lp", -3.0f), "Sub LP", "default");

    component.selectPaneView (rta::view::PaneSelectorButton::Xover);
    auto& xoverPane = dynamic_cast<rta::view::CrossoverPaneView&> (
        const_cast<juce::Component&> (MainComponentTestAccess::pane (component)));
    // Items 1/2 = "main-hp"/"sub-lp"; topology item 2 is LR-4.
    xoverPane.highTraceComboForTest().setSelectedId (1, juce::sendNotificationSync);
    xoverPane.lowTraceComboForTest().setSelectedId (2, juce::sendNotificationSync);
    xoverPane.topologyComboForTest().setSelectedId (2, juce::sendNotificationSync);
    xoverPane.inversionComboForTest().setSelectedId (inversionItemId, juce::sendNotificationSync);

    if (! xoverPane.hasCompleteSelectionForTest())
    {
        std::printf ("FAILED: crossover pane refused -- %s would be the placeholder\n", fileName);
        ++failures;
        return;
    }
    if (! renderComponent (component, outDir, fileName, 1280, 800))
        ++failures;
}

} // namespace

void renderXoverSpecimens (const juce::File& outDir, int& failures)
{
    // Item 1 = NOT INVERTED, item 3 = UNKNOWN (`kInversionChoices`,
    // CrossoverPaneView.cpp). UNKNOWN is round 3's own addition -- the
    // owner's 2026-09-27 "two labelled lines + a warning chip" decision.
    renderOneXoverSpecimen (outDir, 1, "main-live-xover.png", failures);
    renderOneXoverSpecimen (outDir, 3, "main-live-xover-unknown.png", failures);
}

void renderStoreXoverSpecimen (const juce::File& outDir, int& failures)
{
    MainComponent component;
    component.setSyntheticMode (true);

    // Same condition test_main_component_store.cpp's own
    // enableSyntheticWithReference polls for: a real paired hop has to
    // complete before TRANSFER has anything to freeze.
    constexpr int kReferenceTimeoutMs = 3000;
    int waitedMs = 0;
    for (; waitedMs < kReferenceTimeoutMs; waitedMs += 10)
    {
        const auto snapshot = MainComponentTestAccess::analysisThread (component).latest();
        if (snapshot && snapshot->hasReference) break;
        juce::Thread::sleep (10);
    }
    if (waitedMs >= kReferenceTimeoutMs)
    {
        std::printf ("FAILED: no reference fed within %d ms -- main-live-store-xover.png "
                     "cannot prove SYNTHETIC input can produce a TRANSFER reference offscreen\n",
                     kReferenceTimeoutMs);
        ++failures;
        return;
    }

    // The real button seam, pressed twice -- T7's own one-second-resolution
    // naming needs the two presses at least a second apart to read as
    // distinct in the library, though this specimen does not itself inspect
    // the names.
    component.selectPaneView (rta::view::PaneSelectorButton::Transfer);
    MainComponentTestAccess::storeClickedForTest (component);
    juce::Thread::sleep (1100);
    MainComponentTestAccess::storeClickedForTest (component);

    auto& library = MainComponentTestAccess::libraryForTest (component);
    if (library.entries().size() < 2)
    {
        std::printf ("FAILED: fewer than two STORE presses landed in the library -- "
                     "main-live-store-xover.png would have nothing real to pick\n");
        ++failures;
        return;
    }

    component.selectPaneView (rta::view::PaneSelectorButton::Xover);
    auto& xoverPane = dynamic_cast<rta::view::CrossoverPaneView&> (
        const_cast<juce::Component&> (MainComponentTestAccess::pane (component)));
    // The two most recently stored entries, in the pane's own eligible-trace
    // order (CrossoverPaneView::refreshFromLibrary walks library_->entries()
    // in insertion order) -- item 1/2, exactly like renderOneXoverSpecimen's
    // injected-trace case above, except these two REAL traces came from the
    // actual STORE button, not makeXoverTrace().
    xoverPane.highTraceComboForTest().setSelectedId (1, juce::sendNotificationSync);
    xoverPane.lowTraceComboForTest().setSelectedId (2, juce::sendNotificationSync);
    xoverPane.topologyComboForTest().setSelectedId (2, juce::sendNotificationSync);   // LR-4
    xoverPane.inversionComboForTest().setSelectedId (1, juce::sendNotificationSync);  // NOT INVERTED

    if (! xoverPane.hasCompleteSelectionForTest())
    {
        std::printf ("FAILED: crossover pane refused the two real STORE'd traces -- "
                     "main-live-store-xover.png would be the placeholder\n");
        ++failures;
        return;
    }
    if (! renderComponent (component, outDir, "main-live-store-xover.png", 1280, 800))
        ++failures;
}

} // namespace rta::tools
