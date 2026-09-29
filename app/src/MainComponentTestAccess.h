// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. Round of LOW findings on PRs #36/#37, item 2:
// `MainComponent`'s test-only trio (the real pane a click built, the
// analysis thread, and the trace library) used to be public member
// functions -- reachable from any caller, not only the two that actually
// need them. This struct is the one seam: `MainComponent` grants it (and
// only it) friendship, so the three stay private on the class itself and
// every other caller sees the same public surface a shipped build exposes.
//
// Callers, each privileged for a stated reason, not by accident:
//   - app/tests_juce/test_main_component_panes.cpp -- proves the wiring
//     between `selectPaneView` and the real `makePaneFactory`/
//     `analysisThread_` that no lower-level test can see (that file's own
//     header comment).
//   - app/tests_juce/test_main_component_store.cpp -- station-3 STORE
//     (tasks T5/T6/T7): drives the real `storeClicked()` handler and reads
//     back the library/readout/button state no lower-level test can reach.
//   - tools/snapshot.cpp -- drives `analysisThread_` directly to start
//     metering-only SPL logging before rendering main-live-spl.png, the
//     same "the real seam a user's click uses" reasoning `setSyntheticMode`
//     and `selectPaneView` already document there. Not a test binary, but
//     not shipped in `rtatool` either -- a dev/render tool, same footing as
//     the pane test. T8's specimen (main-live-store-xover.png) uses this
//     same seam to drive storeClicked() twice for real.
#pragma once

// Fix round PR #40 LOW F6: this header is test/tool-only (the comment above
// names the exactly two callers). Nothing in `app/src` production code may
// include it -- a stray include there would put every private member it
// reaches one edit away from becoming reachable from the shipped app.
// `RTA_MAINCOMPONENT_TEST_ACCESS` is defined only on `rtatool_main_component_
// tests` and `rtatool_snapshot` (see app/CMakeLists.txt and
// app/tests_juce/CMakeLists.txt); every other target fails to compile the
// instant it includes this file.
#ifndef RTA_MAINCOMPONENT_TEST_ACCESS
#error "MainComponentTestAccess.h is test/tool-only -- see this file's own header comment"
#endif

#include "MainComponent.h"
#include "rta/platform/ChannelConfig.h"

#include <utility>

struct MainComponentTestAccess {
    [[nodiscard]] static const juce::Component& pane(const MainComponent& c) {
        return c.paneComponentForTest();
    }
    // K8 test seam: a multi-pane workspace has more than one child -- this
    // reaches pane `index` directly, the same `workspace_` this struct
    // already has friend access to via `pane()`/`paneSpecsForTest()` above.
    [[nodiscard]] static const juce::Component& paneAtForTest(const MainComponent& c, int index) {
        return *c.workspace_->getChildComponent(index);
    }
    [[nodiscard]] static rta::measure::AnalysisThread& analysisThread(MainComponent& c) {
        return c.analysisThreadForTest();
    }
    // Non-const (app/crossover-pane): test_main_component_panes_xover.cpp
    // calls TraceLibrary::add through this to seed the XOVER pane's stored
    // traces. Every existing caller only ever read through it or compared
    // its address, both still valid against a non-const reference. Renamed
    // from `library` to `libraryForTest` (PR #45 fix round, F1): one
    // whole-word reference to this exact name from a test now covers both
    // this static and `MainComponent::libraryForTest` for
    // tools/orphan_check.py's TEST HOOK category.
    [[nodiscard]] static rta::trace::TraceLibrary& libraryForTest(MainComponent& c) {
        return c.libraryForTest();
    }

    // Fix round MEDIUM F1: the rail-layout regression test's own seam --
    // see app/tests_juce/test_main_component_rail_layout.cpp.
    [[nodiscard]] static const juce::Component& channelRoleTable(const MainComponent& c) {
        return c.rail_.channelRoleTableForTest();
    }
    [[nodiscard]] static const juce::Component& routingMatrix(const MainComponent& c) {
        return c.rail_.routingMatrixForTest();
    }
    // D1/D8 (lane H2): the typed matrix (click cells, read rows), the outer
    // rail viewport and its content, and the names push the device poll makes.
    [[nodiscard]] static rta::view::RoutingMatrix& routingMatrixWidgetForTest(MainComponent& c) {
        return c.rail_.routingMatrixWidgetForTest();
    }
    [[nodiscard]] static const juce::Viewport& railViewportForTest(const MainComponent& c) {
        return c.rail_.railViewportForTest();
    }
    [[nodiscard]] static const juce::Component& railContentForTest(const MainComponent& c) {
        return c.rail_.railContentForTest();
    }
    [[nodiscard]] static const juce::Component& devicePanelForTest(const MainComponent& c) {
        return c.rail_.devicePanelForTest();
    }
    static void applyChannelNamesForTest(MainComponent& c, std::vector<std::string> names) {
        c.applyChannelNames(std::move(names));
    }
    [[nodiscard]] static rta::measure::RoutingPlan currentRoutingPlanForTest(const MainComponent& c) {
        return c.currentRoutingPlan();
    }
    // Prepares the bus as a device with `numChannels` inputs would (what
    // AudioIo::audioDeviceAboutToStart does), so numChannels() is real.
    static void prepareBusForTest(MainComponent& c, double sampleRate, int numChannels) {
        c.audioIo_.bus().prepare(sampleRate, numChannels);
    }

    // Session persistence test seam: app/tests_juce/test_main_component_session.cpp
    // (and its fix-round split, test_main_component_session_fixround.cpp).
    // Every name below ends in `ForTest` and is referenced by that exact
    // spelling from those files -- tools/orphan_check.py's own TEST HOOK
    // rule (a `*ForTest` candidate the linker discards from `rtatool` is
    // exempted only when app/tests* really calls it by that name; see that
    // script's own header comment). PR #45 fix round 3 (PR #43
    // reconciliation checklist item 4): this seam used to ALSO carry its own
    // `mutableLibraryForTest`, reaching `c.library_` directly, alongside
    // `libraryForTest` above reaching the SAME field through `c.
    // libraryForTest()` -- two names for one thing, from the two branches'
    // independent PRs. Collapsed to the one already above; every call site
    // in both session test files now uses it.
    static void saveSessionForTest(MainComponent& c, const juce::File& folder) {
        c.session_.performSave(folder);
    }
    static void openSessionForTest(MainComponent& c, const juce::File& folder) {
        c.session_.performOpen(folder);
    }
    [[nodiscard]] static juce::String readoutForTest(const MainComponent& c) {
        return c.session_.readoutForTest();
    }

    // F6 test seam: app/tests_juce/test_main_component_session_overwrite.cpp.
    // `setConfirmOverwriteForTest` replaces the
    // real, modal AlertWindow with a synchronous stand-in the test controls
    // directly; `maybeConfirmAndSaveForTest` drives the exact check-and-save
    // path `saveClicked()`'s FileChooser callback uses, with no chooser and
    // no message loop involved -- same "drive the real seam a click uses"
    // reasoning this struct already gives for `saveSessionForTest` above.
    static void setConfirmOverwriteForTest(MainComponent& c,
                                          MainComponentSession::ConfirmOverwrite confirm) {
        c.session_.confirmOverwrite_ = std::move(confirm);
    }
    static void maybeConfirmAndSaveForTest(MainComponent& c, const juce::File& folder) {
        c.session_.maybeConfirmAndSave(folder);
    }

    // station-3 STORE test seam: app/tests_juce/test_main_component_store.cpp
    // and tools/snapshot.cpp's T8 specimen. Calls the exact production
    // handler `storeButton_.onClick` is wired to (MainComponent.cpp's
    // constructor) -- not a re-implementation, and not `triggerClick()`
    // (delivered only by a pumped message loop this offscreen harness never
    // runs, same reason MainComponentSession's save/open seam below calls
    // its handler directly rather than through the button).
    static void storeClickedForTest(MainComponent& c) { c.storeClicked(); }
    [[nodiscard]] static juce::String storeReadoutForTest(const MainComponent& c) {
        return c.storeReadout_.getText();
    }
    [[nodiscard]] static bool storeButtonEnabledForTest(const MainComponent& c) {
        return c.storeButton_.isEnabled();
    }
    // T6: reaches the SAME private rebuild session Open drives
    // (MainComponentSession::performOpen -> restorePaneView_ -> this), so a
    // test can prove storeButton_'s enable state follows THIS path too, not
    // only selectPaneView()'s click path.
    static void restoreWorkspaceForTest(MainComponent& c, std::vector<rta::trace::PaneSpec> panes) {
        c.restoreWorkspaceFromSession(std::move(panes));
    }
    // F4 test seam: reads back the LIVE workspace's own pane specs (the same
    // `WorkspaceView::paneSpecs()` Save now calls) -- proves both that Save
    // reads every pane and that Open rebuilt the same shape it was given,
    // without inferring pane count/kind from pixels.
    [[nodiscard]] static std::vector<rta::trace::PaneSpec> paneSpecsForTest(const MainComponent& c) {
        return c.workspace_->paneSpecs();
    }
    // Lets test_main_component_store.cpp force the Reference role back off
    // right after setSyntheticMode(true) -- the only way to reach a REAL,
    // non-null, live Snapshot with hasReference == false (spectrumDb still
    // publishes through the single-channel drainRole path): MainComponent
    // does not otherwise expose audioIo_/bus() to any caller.
    [[nodiscard]] static rta::platform::ChannelConfig& channelConfigForTest(MainComponent& c) {
        return c.audioIo_.bus().config();
    }

    // D7 test seam: app/tests_juce/test_main_component_export.cpp. Drives
    // the real `exportReportButton_.onClick` handler (not a
    // re-implementation), the same "call the exact production handler"
    // reasoning `storeClickedForTest` above already gives.
    static void forceExportThrowForTest(MainComponent& c, bool force) {
        c.forceExportThrowForTest_ = force;
    }
    static void exportReportClickedForTest(MainComponent& c) { c.exportReportClicked(); }
    [[nodiscard]] static juce::String exportReportReadoutForTest(const MainComponent& c) {
        return c.exportReportReadout_.getText();
    }
};
