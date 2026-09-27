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

#include <juce_core/juce_core.h>

#include <functional>

using rta::platform::ChannelConfig;
using rta::platform::ChannelRole;
using rta::view::PaneSelectorButton;
using rta::view::PaneView;

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
    // Owner decision 1: TransferView defaults to MTW, so a TRANSFER store
    // must ALSO say the screen shows MTW.
    CHECK(readout.contains("MTW"));
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
    CHECK(MainComponentTestAccess::storeReadoutForTest(component).isNotEmpty());
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
    const auto& first = library.entries().back();
    CHECK_FALSE(first.name.empty());
    CHECK(first.group == "RTA");  // paneRtaButton_'s own text (T7's own mutant target)

    // T7: two clicks at least one second apart produce two distinct names.
    juce::Thread::sleep(1100);
    MainComponentTestAccess::storeClickedForTest(component);
    const auto& second = library.entries().back();
    CHECK(second.name != first.name);
}
