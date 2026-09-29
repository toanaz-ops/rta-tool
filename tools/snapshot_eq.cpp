// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of tools/. See snapshot_eq.h.
#include "snapshot_eq.h"

#include "SnapshotRender.h"

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "view/EqPaneView.h"

#include "EqTraceFixture.h"  // app/tests: the test suite's own bump trace, so the picture and the assertions share one fixture

#include <cstdio>
#include <string>

namespace rta::tools
{
namespace
{

rta::view::EqPaneView& eqPane (MainComponent& component)
{
    return dynamic_cast<rta::view::EqPaneView&> (
        const_cast<juce::Component&> (MainComponentTestAccess::pane (component)));
}

void renderEqSpecimen (const juce::File& outDir, int& failures)
{
    MainComponent component;
    component.setSyntheticMode (true);
    juce::Thread::sleep (800);

    // The same 8 dB bump at 1 kHz the tests use (EqSessionFixture's
    // makeLinearFixture), seeded BEFORE the pane exists so its first
    // setLibrary fills the combo.
    MainComponentTestAccess::libraryForTest (component)
        .add (eqfixture::makeBumpTrace ("eq-bump"), "TRANSFER @ 12:00:00", "TRANSFER");
    component.selectPaneView (rta::view::PaneSelectorButton::Eq);

    auto& pane = eqPane (component);
    pane.measurementComboForTest().setSelectedId (1, juce::sendNotificationSync);
    pane.autoEqButtonForTest().onClick();

    if (pane.modelForTest().session().committed().empty())
    {
        std::printf ("FAILED: AUTO EQ committed nothing -- main-live-eq.png would be an empty pane\n");
        ++failures;
        return;
    }
    // Wave B: SYNTHETIC stops the device, so VERIFY (and ADOPT) must be dark
    // and the pane must say WHY on screen -- a disabled button with no reason
    // is the failure this specimen exists to show.
    pane.tickForTest();
    if (pane.verifyButtonForTest().isEnabled() || pane.adoptButtonForTest().isEnabled()
        || ! pane.readoutForTest().contains ("VERIFY off:") || ! pane.readoutForTest().contains ("SYNTHETIC"))
    {
        std::printf ("FAILED: SYNTHETIC must leave VERIFY and ADOPT disabled with the reason on screen\n");
        ++failures;
        return;
    }
    if (! renderComponent (component, outDir, "main-live-eq.png", 1280, 800))
        ++failures;
}

void renderStoreEqSpecimen (const juce::File& outDir, int& failures)
{
    MainComponent component;
    component.setSyntheticMode (true);

    // Like renderStoreXoverSpecimen's wait, and one condition longer: a paired
    // hop has to complete before TRANSFER has anything to freeze, AND the
    // transfer block's coherence has to have appeared. Coherence is ABSENT
    // below the engine's effective-average gate (Snapshot.h, TransferBlock), and
    // a trace without it is never offered as an EQ measurement -- so the first
    // hop alone stores a trace this pane must refuse.
    constexpr int kReferenceTimeoutMs = 10000;
    int waitedMs = 0;
    for (; waitedMs < kReferenceTimeoutMs; waitedMs += 10)
    {
        const auto snapshot = MainComponentTestAccess::analysisThread (component).latest();
        if (snapshot && snapshot->hasReference && snapshot->transfer && snapshot->transfer->coherence.has_value()) break;
        juce::Thread::sleep (10);
    }
    if (waitedMs >= kReferenceTimeoutMs)
    {
        std::printf ("FAILED: no reference with coherence within %d ms -- main-live-store-eq.png "
                     "cannot prove SYNTHETIC input can produce an eligible TRANSFER trace offscreen\n",
                     kReferenceTimeoutMs);
        ++failures;
        return;
    }

    component.selectPaneView (rta::view::PaneSelectorButton::Transfer);
    MainComponentTestAccess::storeClickedForTest (component);

    if (MainComponentTestAccess::libraryForTest (component).entries().empty())
    {
        std::printf ("FAILED: the STORE press landed nothing in the library -- "
                     "main-live-store-eq.png would have nothing real to pick\n");
        ++failures;
        return;
    }

    component.selectPaneView (rta::view::PaneSelectorButton::Eq);
    auto& pane = eqPane (component);
    if (pane.measurementIdsForTest().empty())
    {
        std::printf ("FAILED: the stored TRANSFER trace is not eligible as an EQ measurement "
                     "(no coherence) -- main-live-store-eq.png would be the refusal\n");
        ++failures;
        return;
    }
    pane.measurementComboForTest().setSelectedId (1, juce::sendNotificationSync);
    pane.suggestButtonForTest().onClick();

    if (! pane.modelForTest().hasMeasurement() || pane.readoutForTest().isEmpty())
    {
        std::printf ("FAILED: the EQ pane refused the stored trace or left its readout blank -- "
                     "main-live-store-eq.png would prove nothing\n");
        ++failures;
        return;
    }
    if (! renderComponent (component, outDir, "main-live-store-eq.png", 1280, 800))
        ++failures;
}

} // namespace

void renderEqSpecimens (const juce::File& outDir, int& failures)
{
    renderEqSpecimen (outDir, failures);
    renderStoreEqSpecimen (outDir, failures);
}

} // namespace rta::tools
