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
    [[nodiscard]] static const rta::view::DevicePanel& devicePanelTypedForTest(const MainComponent& c) {
        return c.rail_.devicePanelTypedForTest();
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

    // An active bus with no audio callback acknowledging its resync gate: the
    // consumer's wait is set to zero, so the next prepare() ends in a drain on
    // the TIMEOUT path and CaptureBus::resyncAckTimeouts() rises by one.
    static void forceResyncAckTimeoutForTest(MainComponent& c) {
        c.audioIo_.bus().setResyncAckTimeout(std::chrono::milliseconds(0));
        c.audioIo_.bus().setActive(true);
        c.audioIo_.bus().prepare(48000.0, 2);
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
    // L7-EQ wave B: one turn of the 2 Hz poll (`timerCallback`, private). The
    // offscreen harness pumps no message loop, and VERIFY advances only from
    // that callback, so a test drives it by hand exactly as the timer would.
    static void timerTickForTest(MainComponent& c) { c.timerCallback(); }
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

    // Behavioural test flags (lane-end LOW Z8): `forceExportThrowForTest_` is
    // the first flag in a production class that CHANGES what the shipped code
    // does rather than just exposing state. The pattern, for the next author:
    // (1) the flag is a plain private bool, default false, set only through a
    // setter in THIS header, never by production code; (2) it is consumed by
    // one `if` at the top of the branch under test, so the branch itself is
    // the real production code; (3) a test must assert the default (flag off,
    // branch not taken) as well as the forced path, or a flag stuck on would
    // pass every test; (4) its comment in MainComponent.h states what it
    // forces and that it is inert in every shipped build.
    //
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

    // Sweep-IR wave A (T2): drives the exact production LOCATE handler and
    // reads back the suggestion the 2 Hz poll (`timerTickForTest`) produces,
    // with no button click and no message loop -- same "call the production
    // handler" reasoning as `storeClickedForTest`.
    static void locateClickedForTest(MainComponent& c) { c.locateClicked(); }
    [[nodiscard]] static const std::optional<rta::dsp::DelaySuggestion>&
    delaySuggestionForTest(const MainComponent& c) {
        return c.delaySuggestion_;
    }

    // SWEEP wave C (T6): app/tests_juce/test_main_component_sweep*.cpp. The
    // sweep state lives in `MainComponentSweep`, so most of it is reached
    // through its own `*ForTest` members; this struct only hands out the
    // member and the other features' handlers a mutual-exclusion case needs.
    [[nodiscard]] static MainComponentSweep& sweepForTest(MainComponent& c) { return c.sweep_; }
    static void calibrationStartClickedForTest(MainComponent& c) { c.calibrationStartClicked(); }
    [[nodiscard]] static bool locateActiveForTest(const MainComponent& c) {
        return c.locateWaitingForSettle_ || c.locateCaptureArmed_;
    }
    [[nodiscard]] static bool locateCaptureArmedForTest(const MainComponent& c) {
        return c.locateCaptureArmed_;
    }
    [[nodiscard]] static bool calibrationArmedForTest(const MainComponent& c) {
        return c.calibrationCaptureArmed_;
    }
    [[nodiscard]] static juce::String delayReadoutForTest(const MainComponent& c) {
        return c.delayReadout_.getText();
    }
    // The rail rows above and below SWEEP's, for the no-overlap layout case.
    [[nodiscard]] static const juce::Component& delayReadoutComponentForTest(const MainComponent& c) {
        return c.delayReadout_;
    }
    [[nodiscard]] static const juce::Component& calibrationStartButtonForTest(const MainComponent& c) {
        return c.calibrationStartButton_;
    }
    [[nodiscard]] static bool captureBusyForTest(const MainComponent& c) { return c.captureBusy(); }
    // H7 (app/tests_juce/test_main_component_generator*.cpp).
    [[nodiscard]] static MainComponentGenerator& generatorForTest(MainComponent& c) { return c.generator_; }
    [[nodiscard]] static bool outputBusyForTest(const MainComponent& c) { return c.outputBusy(); }
    // T13: pins GEN's level SETTING (not playing) so a fixture settled at -12 dBFS keeps its level
    // while measurements follow GEN; and the one answer a measurement asks for, plus the LOCATE hint.
    static void pinGeneratorLevelForTest(MainComponent& c, double levelDb) {
        auto next = c.generator_.settings();
        next.levelDb = levelDb;
        static_cast<void>(c.generator_.runnerForTest().apply(std::move(next)));
    }
    [[nodiscard]] static rta::measure::MeasurementDrive measurementDriveForTest(const MainComponent& c) {
        return c.measurementDrive();
    }
    [[nodiscard]] static juce::String locateHintForTest(const MainComponent& c) {
        return c.locateButton_.getProperties().getWithDefault(az::ui::hintProperty, juce::String()).toString();
    }
    static void calibrationEndClickedForTest(MainComponent& c) { c.calibrationEndClicked(); }
    // The engine the loopback renders, so a test can prove an aborted run left
    // the output disarmed (render a few blocks by hand and ask it), or install
    // a foreign source ahead of a press.
    [[nodiscard]] static rta::platform::OutputEngine& outputEngineForTest(MainComponent& c) {
        return c.audioIo_.output();
    }
    [[nodiscard]] static juce::String calibrationReadoutForTest(const MainComponent& c) {
        return c.calibrationReadout_.getText();
    }
    [[nodiscard]] static rta::platform::OutputRole outputRoleForTest(const MainComponent& c, int channel) {
        return c.audioIo_.output().role(channel);
    }
    [[nodiscard]] static bool outputSourceQuiescentForTest(const MainComponent& c) {
        return c.audioIo_.output().sourceIsQuiescent();
    }

    // ALIGN-UI wave A (T6): app/tests_juce/test_main_component_align*.cpp and
    // tools/snapshot_align.cpp. The runner lives in `MainComponentAlign`, so
    // most of it is reached through its own `*ForTest` members.
    [[nodiscard]] static MainComponentAlign& alignForTest(MainComponent& c) { return c.align_; }
    static void applyClickedForTest(MainComponent& c) { c.applyClicked(); }
    // Replaces SYNTHETIC's two output paths (the fixture the plan derives its
    // bounds from: noise off, an LR4 pair 96 samples apart). The roles the
    // bus carries are untouched; only the writer thread is rebuilt.
    static void restartSyntheticForTest(MainComponent& c, rta::measure::SyntheticInput::Config config) {
        c.syntheticInput_.reset();  // joins the thread before the new one is built
        config.loopback = &c.audioIo_.output();
        c.syntheticInput_ = std::make_unique<rta::measure::SyntheticInput>(c.audioIo_.bus(), config);
        waitForResyncForTest(c);
    }
    // prepare() gates the producer until AnalysisThread has emptied the rings
    // (CaptureBus.h, "Resync"): audio written meanwhile is counted, not kept.
    // A test that starts a sweep right after prepare() would lose the head of
    // it to that window, which a person at the controls never does, so wait
    // (bounded) for the gate to lower first.
    static void waitForResyncForTest(MainComponent& c) {
        const auto deadline = juce::Time::getMillisecondCounter() + 3000u;
        while (c.audioIo_.bus().resyncPending() && juce::Time::getMillisecondCounter() < deadline) {
            juce::Thread::sleep(2);
        }
    }
    /// Refuses one oversize block on the bus (bigger than a ring, so nothing is written
    /// and no ring index moves): the drop counter rises exactly as a starved consumer
    /// would raise it, without racing the feed thread's own writes.
    static void injectBusDropForTest(MainComponent& c) {
        const std::vector<float> oversize(1u << 20, 0.0f);
        const float* channels[2] = { oversize.data(), oversize.data() };
        c.audioIo_.bus().pushFromCallback(channels, 2, static_cast<int>(oversize.size()));
    }
    /// Frames the capture bus refused (ring full) on channel `ch` since it was built.
    [[nodiscard]] static std::uint64_t busDropsForTest(MainComponent& c, int ch) {
        return c.audioIo_.bus().dropCount(ch);
    }

    // RT60-UI wave A (T5): app/tests_juce/test_main_component_rt60.cpp and
    // tools/snapshot_rt60.cpp. The selection, the cached analysis and the
    // output/TAIL settings live in `MainComponentRt60`.
    [[nodiscard]] static MainComponentRt60& rt60ForTest(MainComponent& c) { return c.rt60_; }
};
