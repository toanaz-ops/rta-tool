// SPDX-License-Identifier: AGPL-3.0-or-later
//
// station-3 STORE plan tasks T5/T6/T7 (docs/plans/2026-09-27-store-lane-plan.md)
// against a REAL MainComponent -- same shape as test_main_component_panes.cpp
// beside this file: setSyntheticMode(true) drives the whole analysis chain
// from a deterministic in-process feed, no device and no message loop needed.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "rta/platform/ChannelConfig.h"
#include "view/TransferSourceToggle.h"
#include "view/TransferView.h"

#include <juce_core/juce_core.h>

#include <functional>

using rta::platform::ChannelConfig;
using rta::platform::ChannelRole;
using rta::view::PaneSelectorButton;
using rta::view::PaneView;
using rta::view::TransferPane;
using rta::view::TransferView;

namespace {

/// Same shape test_main_component_panes.cpp's own waitUntil gives: polls
/// rather than sleeping a fixed, arbitrary interval and hoping.
bool waitUntil(const std::function<bool()>& predicate, int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        if (predicate()) return true;
        juce::Thread::sleep(5);
    }
    return predicate();
}

/// Enables SYNTHETIC mode and waits for a live Snapshot whose `hasReference`
/// is true -- the TRANSFER-with-a-fed-reference case (T5 acceptance (b)).
void enableSyntheticWithReference(MainComponent& component) {
    component.setSyntheticMode(true);
    REQUIRE(waitUntil(
        [&] {
            const auto snapshot = MainComponentTestAccess::analysisThread(component).latest();
            return snapshot && snapshot->hasReference;
        },
        3000));
}

/// Enables SYNTHETIC mode, then immediately forces the Reference role back to
/// Unused -- before drainPaired ever sees channel 1 configured as a
/// reference, spectrumDb still publishes through the single-channel
/// drainRole path (Analyser.h's own comment: analysers_[0] "also serves the
/// plain single-channel RTA path... when no route exists at all"). This is
/// the only way to reach a REAL, non-null, live Snapshot with
/// `hasReference == false` -- MainComponent does not otherwise expose
/// audioIo_/bus() to any caller (T5 acceptance (c)).
void enableSyntheticWithoutReference(MainComponent& component) {
    component.setSyntheticMode(true);
    MainComponentTestAccess::channelConfigForTest(component).setRole(1, ChannelRole::Unused);
    REQUIRE(waitUntil(
        [&] {
            const auto snapshot = MainComponentTestAccess::analysisThread(component).latest();
            return snapshot && !snapshot->spectrumDb.empty();
        },
        3000));
    // Never actually latched a reference in the window before the role was
    // removed -- guards the fixture itself, not the production code.
    const auto snapshot = MainComponentTestAccess::analysisThread(component).latest();
    REQUIRE(snapshot);
    REQUIRE_FALSE(snapshot->hasReference);
}

/// The real, live TRANSFER pane -- same `dynamic_cast` through
/// `MainComponentTestAccess::pane` snapshot_xover.cpp already uses for
/// XOVER, non-const because the fix-round-1 tests below drive the REAL
/// `TransferSourceToggle` click handlers (`clickFixedButton()`), not a
/// second, injected way of changing which engine a pane shows.
TransferView& liveTransferView(MainComponent& component) {
    return dynamic_cast<TransferView&>(
        const_cast<juce::Component&>(MainComponentTestAccess::pane(component)));
}

/// Same as `liveTransferView` above but for pane `index` of a multi-pane
/// workspace (K8's own test below).
TransferView& transferViewAt(MainComponent& component, int index) {
    return dynamic_cast<TransferView&>(
        const_cast<juce::Component&>(MainComponentTestAccess::paneAtForTest(component, index)));
}

}  // namespace

TEST_CASE("STORE on the RTA pane freezes a magnitude-only trace", "[main_component_store]") {
    MainComponent component;
    enableSyntheticWithReference(component);  // spectrumDb is populated either way
    component.selectPaneView(PaneSelectorButton::Rta);

    auto& library = MainComponentTestAccess::libraryForTest(component);
    const auto sizeBefore = library.entries().size();
    MainComponentTestAccess::storeClickedForTest(component);

    REQUIRE(library.entries().size() == sizeBefore + 1);
    const auto& newEntry = library.entries().back();
    const auto* trace = library.trace(newEntry.traceId);
    REQUIRE(trace != nullptr);
    CHECK_FALSE(trace->has(rta::trace::Field::Phase));
}

TEST_CASE("STORE on TRANSFER with a fed reference freezes a trace with phase",
         "[main_component_store]") {
    MainComponent component;
    enableSyntheticWithReference(component);
    component.selectPaneView(PaneSelectorButton::Transfer);

    auto& library = MainComponentTestAccess::libraryForTest(component);
    const auto sizeBefore = library.entries().size();
    MainComponentTestAccess::storeClickedForTest(component);

    REQUIRE(library.entries().size() == sizeBefore + 1);
    const auto& newEntry = library.entries().back();
    const auto* trace = library.trace(newEntry.traceId);
    REQUIRE(trace != nullptr);
    CHECK(trace->has(rta::trace::Field::Phase));

    const auto readout = MainComponentTestAccess::storeReadoutForTest(component);
    CHECK(readout.contains("FIXED FFT"));
    // Owner decision 1, fix round 1 HIGH F1: TransferView's own DEFAULT is
    // MTW for every pane, and nothing here toggled it -- so all three are
    // named, not a bare constant "screen shows MTW".
    CHECK(readout.contains("screen shows MTW on"));
    CHECK(readout.contains("MAG"));
    CHECK(readout.contains("PHASE"));
    CHECK(readout.contains("COH"));
}

// Fix round 1, HIGH F1: the readout used to append "-- screen shows MTW" on
// EVERY TRANSFER store, unconditionally -- an operator can flip any pane to
// FIXED with the shipped TransferSourceToggle (TransferSourceToggle.cpp:47-48
// -> TransferView::setSource), and the readout kept lying. These three cases
// are the ones the fix round named explicitly.
TEST_CASE("STORE readout on TRANSFER names exactly the panes currently showing MTW",
         "[main_component_store]") {
    MainComponent component;
    enableSyntheticWithReference(component);
    component.selectPaneView(PaneSelectorButton::Transfer);
    auto& transferView = liveTransferView(component);

    SECTION("all three panes on FIXED: no MTW clause at all") {
        // The mutant this case must catch: reverting to an unconditional
        // "-- screen shows MTW" append. With every pane explicitly FIXED
        // (and the fixed block present, since a reference is fed), that
        // mutant's readout would still say MTW; the fixed implementation's
        // must not.
        transferView.sourceToggle(TransferPane::Magnitude).clickFixedButton();
        transferView.sourceToggle(TransferPane::Phase).clickFixedButton();
        transferView.sourceToggle(TransferPane::Coherence).clickFixedButton();

        MainComponentTestAccess::storeClickedForTest(component);
        const auto readout = MainComponentTestAccess::storeReadoutForTest(component);
        CHECK(readout.contains("FIXED FFT"));
        CHECK_FALSE(readout.contains("MTW"));
    }

    SECTION("mixed: MAG and COH stay MTW, PHASE is FIXED -- only MAG and COH are named") {
        transferView.sourceToggle(TransferPane::Phase).clickFixedButton();

        MainComponentTestAccess::storeClickedForTest(component);
        const auto readout = MainComponentTestAccess::storeReadoutForTest(component);
        CHECK(readout.contains("screen shows MTW on"));
        CHECK(readout.contains("MAG"));
        CHECK(readout.contains("COH"));
        CHECK_FALSE(readout.contains("PHASE"));
    }
}

TEST_CASE("STORE on TRANSFER with no reference fed refuses and leaves the library untouched",
         "[main_component_store]") {
    MainComponent component;
    enableSyntheticWithoutReference(component);
    component.selectPaneView(PaneSelectorButton::Transfer);

    auto& library = MainComponentTestAccess::libraryForTest(component);
    const auto sizeBefore = library.entries().size();
    MainComponentTestAccess::storeClickedForTest(component);

    CHECK(library.entries().size() == sizeBefore);
    // The call site's OWN guard (MainComponentStore.cpp), independent of
    // CaptureConverter's copy -- checked by its own distinct wording so a
    // mutant deleting JUST this guard (T5's own named mutant) is caught even
    // though CaptureConverter's internal check would still refuse and leave
    // the library untouched either way.
    CHECK(MainComponentTestAccess::storeReadoutForTest(component).contains("no reference fed"));
}

TEST_CASE("two STORE clicks in the same pane produce two distinct trace ids",
         "[main_component_store]") {
    MainComponent component;
    enableSyntheticWithReference(component);
    component.selectPaneView(PaneSelectorButton::Rta);

    auto& library = MainComponentTestAccess::libraryForTest(component);
    MainComponentTestAccess::storeClickedForTest(component);
    MainComponentTestAccess::storeClickedForTest(component);

    REQUIRE(library.entries().size() >= 2);
    const auto& a = library.entries()[library.entries().size() - 2];
    const auto& b = library.entries()[library.entries().size() - 1];
    CHECK(a.traceId != b.traceId);
}

TEST_CASE("STORE button enable state follows the pane, via click and via session restore",
         "[main_component_store]") {
    MainComponent component;
    component.setSyntheticMode(true);

    // T6: the click path (selectPaneView, MainComponentPanes.cpp).
    component.selectPaneView(PaneSelectorButton::Rta);
    CHECK(MainComponentTestAccess::storeButtonEnabledForTest(component));
    component.selectPaneView(PaneSelectorButton::Transfer);
    CHECK(MainComponentTestAccess::storeButtonEnabledForTest(component));
    component.selectPaneView(PaneSelectorButton::Spl);
    CHECK_FALSE(MainComponentTestAccess::storeButtonEnabledForTest(component));
    component.selectPaneView(PaneSelectorButton::Xover);
    CHECK_FALSE(MainComponentTestAccess::storeButtonEnabledForTest(component));
    // Back to an ENABLED pane via the click path before the restore-path
    // block below: only starting from enabled does a missing guard on
    // restoreWorkspaceFromSession (which would leave the previous state
    // untouched instead of disabling it) actually flip the next check red.
    component.selectPaneView(PaneSelectorButton::Rta);
    CHECK(MainComponentTestAccess::storeButtonEnabledForTest(component));

    // T6: the session-Open path (restoreWorkspaceFromSession) -- the exact
    // omission this shape already bit once for the pane selector buttons
    // themselves (MainComponentPanes.cpp:133's own comment).
    MainComponentTestAccess::restoreWorkspaceForTest(component, {rta::trace::PaneSpec{"xover", 1.0f}});
    CHECK_FALSE(MainComponentTestAccess::storeButtonEnabledForTest(component));
    MainComponentTestAccess::restoreWorkspaceForTest(component, {rta::trace::PaneSpec{"rta", 1.0f}});
    CHECK(MainComponentTestAccess::storeButtonEnabledForTest(component));
}

TEST_CASE("STORE names and groups follow the pane label, distinctly per click",
         "[main_component_store]") {
    MainComponent component;
    enableSyntheticWithReference(component);
    component.selectPaneView(PaneSelectorButton::Rta);

    auto& library = MainComponentTestAccess::libraryForTest(component);
    MainComponentTestAccess::storeClickedForTest(component);
    // Copied BY VALUE, not held as a reference: the second storeClickedForTest
    // call below push_back()s into the same TraceLibrary, which may reallocate
    // its backing vector and invalidate any reference taken before it.
    const auto first = library.entries().back();
    CHECK_FALSE(first.name.empty());
    CHECK(first.group == "RTA");  // paneRtaButton_'s own text (T7's own mutant target)

    // T7: two clicks at least one second apart produce two distinct names.
    juce::Thread::sleep(1100);
    MainComponentTestAccess::storeClickedForTest(component);
    const auto second = library.entries().back();
    CHECK(second.name != first.name);
}

// K8 (docs/HUMAN-QA-QUEUE.md, PR #51 round-2 R6): with two TRANSFER panes in
// one workspace, the old `findTransferView()` returned on the FIRST
// `dynamic_cast` match, so the STORE readout's MTW clause described only
// that one pane -- the second pane's MTW state went unreported.
TEST_CASE("STORE readout on a two-TRANSFER-pane workspace names both panes, distinctly",
         "[main_component_store]") {
    MainComponent component;
    enableSyntheticWithReference(component);
    MainComponentTestAccess::restoreWorkspaceForTest(
        component, {rta::trace::PaneSpec{"transfer", 1.0f}, rta::trace::PaneSpec{"transfer", 1.0f}});
    REQUIRE(component.currentPaneView() == PaneView::Transfer);

    // Pane 1 (index 0) stays at every plot's MTW default. Pane 2 (index 1)
    // flips PHASE to FIXED -- pane 2's own clause must therefore omit PHASE
    // while pane 1's still names it, proving the readout is genuinely PER
    // PANE, not one summary that happens to be right for pane 1 alone (the
    // mutant this test catches: `findTransferView` returning only the first
    // match would produce exactly pane 1's clause and say nothing about
    // pane 2 at all).
    transferViewAt(component, 1).sourceToggle(TransferPane::Phase).clickFixedButton();

    MainComponentTestAccess::storeClickedForTest(component);
    const auto readout = MainComponentTestAccess::storeReadoutForTest(component);
    INFO(readout.toStdString());

    CHECK(readout.contains("TRANSFER 1"));
    CHECK(readout.contains("TRANSFER 2"));
    const auto pane1Start = readout.indexOf("TRANSFER 1");
    const auto pane2Start = readout.indexOf("TRANSFER 2");
    REQUIRE(pane1Start >= 0);
    REQUIRE(pane2Start > pane1Start);
    const auto pane1Clause = readout.substring(pane1Start, pane2Start);
    const auto pane2Clause = readout.substring(pane2Start);

    CHECK(pane1Clause.contains("MAG"));
    CHECK(pane1Clause.contains("PHASE"));
    CHECK(pane1Clause.contains("COH"));
    CHECK(pane2Clause.contains("MAG"));
    CHECK(pane2Clause.contains("COH"));
    CHECK_FALSE(pane2Clause.contains("PHASE"));
}
