// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.6 (Wave E / T10).
//
// MainComponent's own design rationale (T0, 2026-09-29: moved here from the
// class comment in MainComponent.h to bring that header under the project's
// 400-line cap -- comment text only, unchanged in substance).
//
// Composition root for one screen, not a DSP class and not a drawing class
// beyond its own masthead and background. Left rail carries a LIVE/SYNTHETIC
// mode switch, the device panel and the channel role table; the rest of the
// window is `workspace_`, a 1..3 pane stack fed by `analysisThread_`
// regardless of which source is currently writing into the bus underneath
// it, plus `library_`, the stored-trace library every pane in it can draw
// from (task 10: the seam that stayed unreached for a whole session --
// docs/HANDOFF.md).
//
// ## Trap T-1: member declaration order is load-bearing
//
// `audioIo_` is declared BEFORE `analysisThread_` and `syntheticInput_`.
// Members are destroyed in REVERSE declaration order, so both threads --
// which keep reading `audioIo_.bus()` until their own destructors return --
// are torn down before the bus (and the device feeding it) dies. Both
// thread classes also call `stopThread()` in their own destructors as a
// second, independent guarantee of the same thing (belt and braces, per the
// plan): getting this wrong is a crash that happens only at shutdown, on a
// customer's machine, once.
//
// The same rule extends to `library_` and `workspace_`: `workspace_` holds a
// raw, non-owning pointer into `library_` (handed over by `setLibrary`),
// so `library_` must be declared BEFORE `workspace_`. Declared earlier means
// destroyed LATER (reverse declaration order again): `workspace_` -- and
// every child pane torn down inside it -- unwinds first, while `library_` is
// still alive, and only then does `library_` itself go. Reversing the two
// would leave a child pane's destructor holding a pointer into an
// already-destroyed library.
//
// ## One bus, one reader, two possible writers
//
// `analysisThread_` is constructed once, against `audioIo_.bus()`, and never
// re-pointed: it reads whatever the bus's rings hold regardless of whether
// `audioIo_`'s real device callback or `syntheticInput_`'s thread is the one
// filling them. `setSyntheticMode()` is what makes sure exactly one of those
// two is ever active at a time -- `CaptureBus::prepare()`'s own precondition
// is that nothing else is concurrently writing.
//
// `apiServer_` (lane L-API Task J) is declared AFTER `analysisThread_` for
// the same reverse-destruction reason: it holds `analysisThread_` as a
// `SnapshotSource&`, so it must stop and join BEFORE the source it reads is
// destroyed. It is a `unique_ptr` rather than a by-value member for one
// reason: the API is off by default (record sec.8), and a null pointer is
// the honest spelling of "the operator did not ask for this".
#include "MainComponent.h"

#include "trace/Workspace.h"

namespace {

// SyntheticInput has no device of its own to name channels after (a device
// panel is not involved), so this class names them itself. Two names, not
// one per role: SyntheticInput writes two channels regardless of which
// role(s) the table below assigns to them.
const std::vector<std::string> kSyntheticChannelNames{"Synthetic L", "Synthetic R"};

}  // namespace

// makePaneFactory itself now lives in PaneFactory.h/.cpp (included via
// MainComponent.h) -- see that header's own comment for why it was split
// out (fix round, PR #26: a test needs to call the REAL production closure,
// and this function needs none of MainComponent's own heavy dependencies).

MainComponent::MainComponent()
    : analysisThread_(audioIo_.bus(), rta::measure::Analyser::Config{}),
      // No device open yet, so no input channels to show a row for: the
      // routing matrix's rows follow the device's channel list from the first
      // refreshChannelNamesFromDevice() on (D1, lane H2), not a fixed 8.
      rail_(audioIo_, 0),
      // The default workspace when none has been loaded: exactly one `rta`
      // pane, so the app's opening screen stays byte-for-byte what it was
      // before this task (task brief, step 3). Nothing in this class loads
      // a session yet. The pane selector (`selectPaneView`) can rebuild this
      // to a different single pane; the shape it starts in is unchanged.
      workspace_(std::make_unique<rta::view::WorkspaceView>(
          std::vector<rta::trace::PaneSpec>{rta::trace::PaneSpec{}}, makePaneFactory(analysisThread_, eq_.binding()))),
      // Session persistence: Save reads EVERY pane in `workspace_` (F4,
      // docs/HUMAN-QA-QUEUE.md D11) through `WorkspaceView::paneSpecs()`,
      // via a callback rather than a stored pointer, matching
      // makePaneFactory's own "hand over exactly the capability needed"
      // shape; Open's rebuild is restoreWorkspaceFromSession (MainComponentPanes.cpp).
      // F6's overwrite confirmation is left at its default (the real
      // AlertWindow) -- production has no reason to override it.
      session_(
          library_, [this] { return workspace_->paneSpecs(); },
          [this](std::vector<rta::trace::PaneSpec> panes) { return restoreWorkspaceFromSession(std::move(panes)); }) {
    modeSwitch_.setClickingTogglesState(true);
    modeSwitch_.getProperties().set(az::ui::hintProperty, "no hardware needed");
    modeSwitch_.onClick = [this] { modeSwitchClicked(); };
    addAndMakeVisible(modeSwitch_);

    locateButton_.getProperties().set(az::ui::hintProperty, "pink noise, output ch 1, one-shot");
    locateButton_.onClick = [this] { locateClicked(); };
    addAndMakeVisible(locateButton_);

    applyButton_.setEnabled(false);
    applyButton_.onClick = [this] { applyClicked(); };
    addAndMakeVisible(applyButton_);

    delayReadout_.setText("delay: --", juce::dontSendNotification);
    delayReadout_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(delayReadout_);

    // L6a Wave 3 (record §8): the calibration flow. IEC 60942's 94.0 dB
    // nominal is what these two buttons offer -- an operator-typed level is
    // CalibrationSession's own affordance (A4) and has no text-entry field in
    // this pass; wiring one is a UI addition, not a flow one.
    calibrationStartButton_.getProperties().set(az::ui::hintProperty,
                                                "94 dB calibrator, route 0's mic channel");
    calibrationStartButton_.onClick = [this] { calibrationStartClicked(); };
    addAndMakeVisible(calibrationStartButton_);

    calibrationEndButton_.getProperties().set(az::ui::hintProperty,
                                              "same calibrator, no adjustment since START");
    calibrationEndButton_.onClick = [this] { calibrationEndClicked(); };
    addAndMakeVisible(calibrationEndButton_);

    calibrationReadout_.setText("calibration: not started", juce::dontSendNotification);
    calibrationReadout_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(calibrationReadout_);

    // Task W2-E2b part B: reachable, minimal -- one button beside the
    // calibration row rather than a new pane.
    exportReportButton_.getProperties().set(az::ui::hintProperty,
                                            "writes report.html into this session's SPL folder");
    exportReportButton_.onClick = [this] { exportReportClicked(); };
    addAndMakeVisible(exportReportButton_);

    exportReportReadout_.setText("export: no SPL session logged yet", juce::dontSendNotification);
    exportReportReadout_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(exportReportReadout_);

    // station-3 STORE (T5): one global button, enabled on RTA/TRANSFER and
    // disabled on SPL/XOVER by selectPaneView()/restoreWorkspaceFromSession()
    // (MainComponentPanes.cpp, T6) -- currentPaneView_ starts at Rta, so the
    // button starts enabled to match.
    storeButton_.getProperties().set(az::ui::hintProperty, "freezes the current RTA/TRANSFER curve");
    storeButton_.onClick = [this] { storeClicked(); };
    addAndMakeVisible(storeButton_);

    storeReadout_.setText("store: nothing frozen yet", juce::dontSendNotification);
    storeReadout_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(storeReadout_);

    // Session persistence: the seam this task exists for -- SessionStore/
    // SessionCodec were built and tested in lane L5a with no caller anywhere
    // in app/src until now.
    session_.attachTo(*this);
    wireEqVerify();

    rail_.attachTo(*this);

    // Owner decision 2026-09-26: the pane selector (MainComponentPanes.cpp).
    wirePaneSelectorButtons();

    addAndMakeVisible(*workspace_);
    // The seam this whole task exists for (docs/HANDOFF.md): a library was
    // built in L5a, a view could draw one, and nothing ever called this.
    workspace_->setLibrary(&library_);

    refreshChannelNamesFromDevice();

    // Slow poll: only watches for the device's own channel list changing
    // (a device opened, closed or swapped from devicePanel_) -- DevicePanel
    // and every pane in workspace_ already run their own faster timers for
    // the state that actually needs one.
    startTimerHz(2);

    // --- lane L-API Task J: the read-only remote API ------------------------
    // Constructed LAST in this body and declared after `analysisThread_`, so
    // shutdown runs in the only order that is safe: the server stops and
    // joins, then the `SnapshotSource` it held a reference to dies.
    //
    // The settings are a plain struct built right here, with no preferences
    // store behind them -- record sec.15 R5: none exists anywhere in `app/`,
    // and this lane does not build one to hold nine fields. `enabled` is
    // false, so on a shipped build nothing binds, no thread starts and
    // NOTHING OBSERVABLE CHANGES for an operator who did not ask for the
    // API, which is the whole point of the default. To try it, flip the one
    // line below and follow Task J's manual check in
    // docs/plans/2026-09-17-remote-api-impl-plan.md.
    rta::api::ApiSettings apiSettings;   // enabled=false, 127.0.0.1, port 4736
    apiServer_ = std::make_unique<rta::api::ApiServer>(analysisThread_, apiSettings);
}

MainComponent::~MainComponent() {
    stopTimer();
    // Explicit, and before any other member unwinds. Reverse declaration
    // order already guarantees this (`apiServer_` is declared after
    // `analysisThread_`), so this line is the second, independent guarantee
    // -- exactly the belt and braces trap T-1's class comment argues for on
    // the two audio threads, and for the same reason: getting shutdown wrong
    // is a crash that happens once, at exit, on a customer's machine.
    apiServer_.reset();
}

void MainComponent::setSyntheticMode(bool enabled) {
    if (enabled == isSyntheticMode()) {
        return;
    }

    if (enabled) {
        // CaptureBus::prepare() -- called from SyntheticInput's own
        // constructor -- has "no concurrent writer" as its precondition.
        // Stop any live device first so the real callback and the synthetic
        // thread never race the same bus.
        audioIo_.stop();
        rail_.setDevicePanelEnabled(false);

        // The synthetic knobs, ON: task 6 built `measurementDelaySamples` /
        // `measurementNoiseDb` and left both at their inert defaults, which
        // made the hardware-free path show H = 1 forever -- flat 0.0 dB,
        // 0 degrees, coherence 1.00, the single most misleading picture this
        // app can display (it is what a perfectly working measurement of
        // nothing looks like, AND what several broken engines look like).
        // 4 samples at 48 kHz gives a closed form that is readable on
        // screen: phi(f) = -360*f*D/fs is -30 degrees at 1 kHz and -120 at
        // 4 kHz; -30 dB of independent noise on the measurement channel
        // pulls coherence down visibly wherever the noise floor dominates,
        // without erasing the transfer function entirely.
        rta::measure::SyntheticInput::Config syntheticConfig;
        syntheticConfig.measurementDelaySamples = 4;
        syntheticConfig.measurementNoiseDb = -30.0;
        syntheticInput_ = std::make_unique<rta::measure::SyntheticInput>(audioIo_.bus(), syntheticConfig);

        // A real role assignment, not the "either channel" default that was
        // sound only while the Config above was impairment-free: with a
        // real delay and a real noise floor, the Measurement-role channel no
        // longer matches the Reference-role one by construction (comment
        // this replaced said as much), and `AnalysisThread` needs an
        // explicit `Reference` channel to compute a transfer function at all
        // -- `firstChannelWithRole(ChannelRole::Reference)` finds nothing on
        // a channel left at its `Unused` default (ChannelConfig.h). Channel
        // 0 carries the impaired signal, channel 1 the clean one
        // (`SyntheticInput::runBody` writes by role, not by a fixed index,
        // so which physical channel gets which role is this call's choice
        // alone).
        audioIo_.bus().config().setRole(0, rta::platform::ChannelRole::Measurement);
        audioIo_.bus().config().setRole(1, rta::platform::ChannelRole::Reference);

        lastChannelNames_ = kSyntheticChannelNames;
        rail_.setChannelNames(lastChannelNames_);
        // routingMatrix_ caches cell TEXT rather than reading config_ live
        // at paint time the way channelRoleTable_'s ListBox does
        // (RoutingMatrix.h's own class comment: refreshFromConfig() is
        // "production API" precisely because nothing calls it
        // automatically) -- without this, the two role assignments just
        // above would show as UNUSED here until the next timerCallback tick
        // or an operator's own click.
        rail_.refreshFromConfig();
    } else {
        // Order matters: destroy the synthetic writer before re-enabling the
        // panel that lets a user start a real one, so there is never a
        // window where both could be live at once.
        syntheticInput_.reset();

        // The roles set on entry do NOT unset themselves: CaptureBus::prepare()
        // resets ring CONTENTS on a device change, never ChannelConfig's roles
        // (they are separate state -- ChannelConfig.h's own class comment
        // draws this line explicitly, two different epochs for two different
        // things). Left alone, a stale Reference role survives straight into
        // LIVE mode and breaks it two different ways depending on the device:
        // on a MONO device, AnalysisThread::drain() still finds a Reference
        // role configured, takes the paired branch, finds bus_.ring(1) null
        // for a channel the device never prepared, and drainPaired() returns
        // immediately -- NOTHING drains, including channel 0's measurement,
        // and the plot freezes with no channel-1 row in the table for a user
        // to clear the role from. On a STEREO device the same staleness
        // silently starts a transfer measurement between two live inputs
        // nobody asked for. Reset to Unused, not to whatever the role table
        // held before synthetic mode started: this class has never offered
        // a way to remember or restore a prior live assignment, and Unused is
        // what channelRoleTable_ shows below anyway once
        // refreshChannelNamesFromDevice() repopulates it from the real
        // device's channel list.
        audioIo_.bus().config().setRole(0, rta::platform::ChannelRole::Unused);
        audioIo_.bus().config().setRole(1, rta::platform::ChannelRole::Unused);

        rail_.setDevicePanelEnabled(true);
        lastChannelNames_.clear();
        refreshChannelNamesFromDevice();
        rail_.refreshFromConfig();
    }

    modeSwitch_.setToggleState(enabled, juce::dontSendNotification);
    repaint();
}

void MainComponent::modeSwitchClicked() {
    // setClickingTogglesState(true) flips the button's own toggle state
    // before onClick fires, so getToggleState() already reads the mode the
    // user just asked for.
    setSyntheticMode(modeSwitch_.getToggleState());
}

// locateClicked / pollLocatePipeline / updateDelayReadout / applyClicked:
// MainComponentDelay.cpp (this file's own 400-line cap split).
//
// calibrationStartClicked / calibrationEndClicked / pollCalibrationPipeline /
// updateCalibrationReadout / restartSplLoggingForCalibration /
// writeCalibrationRecordAndUpdateInvalidFlag: MainComponentCalibration.cpp,
// the same split.
//
// startFreshSplLog / startFreshSplLogWithConfig / pollSplLogging /
// exportReportClicked: MainComponentSpl.cpp, the same split.
//
// storeClicked: MainComponentStore.cpp (station-3 STORE task T5), the same
// split.

void MainComponent::timerCallback() {
    // routingMatrix_ caches its cell text (RoutingMatrix.h's own class
    // comment) rather than reading rta::platform::ChannelConfig live at
    // paint time, so it needs an explicit poke to notice a role change made
    // anywhere OTHER than its own click -- channelRoleTable_'s clicks in
    // LIVE mode, chiefly. Cheap: one text cell per channel (at most
    // kMaxChannels = 64), twice a second.
    rail_.refreshFromConfig();
    refreshMembershipFromSnapshot();
    pollLocatePipeline();
    pollCalibrationPipeline();
    eq_.pollVerify();  // L7-EQ VERIFY: before the SYNTHETIC early return below
    pollSplLogging();

    if (isSyntheticMode()) {
        return;  // fixed list, set once in setSyntheticMode()
    }
    refreshChannelNamesFromDevice();
}

void MainComponent::refreshChannelNamesFromDevice() {
    applyChannelNames(audioIo_.currentState().inputChannelNames);
}

void MainComponent::applyChannelNames(std::vector<std::string> names) {
    if (names != lastChannelNames_) {
        // A device that reports a NON-ZERO channel count leaves no row for a
        // channel past it, so a role/tf still held there could never be seen
        // or cleared again -- and a stale role is not harmless: with no valid
        // Measurement left, AnalysisThread::drainRole finds no ring for it
        // and the RTA plot freezes under an all-UNUSED table (the failure
        // setSyntheticMode already documents for its own case). Clear them,
        // exactly as setSyntheticMode does. A count of ZERO is a transient
        // "device closed" (a sample-rate change reopens it), not a smaller
        // device: roles are kept so the operator's assignment survives it.
        if (!names.empty()) {
            auto& config = audioIo_.bus().config();
            const int count = static_cast<int>(
                std::min(names.size(), static_cast<std::size_t>(rta::platform::kMaxChannels)));
            for (int ch = count; ch < rta::platform::kMaxChannels; ++ch) {
                if (config.role(ch) != rta::platform::ChannelRole::Unused) {
                    config.setRole(ch, rta::platform::ChannelRole::Unused);
                }
                if (config.transferFunction(ch) != 0) config.setTransferFunction(ch, 0);
            }
        }
        lastChannelNames_ = std::move(names);
        // Feeds channelRoleTable_ AND routingMatrix_ (MainComponentRail.h).
        rail_.setChannelNames(lastChannelNames_);
    }
}

void MainComponent::refreshMembershipFromSnapshot() {
    // The AVG column reflects the live AverageGroup membership from the
    // most recently PUBLISHED Snapshot, read through the same
    // planRouting() AnalysisThread itself used to build it -- recomputed
    // here rather than stored, because it is a pure function of config_
    // (RoutingPlan.h's own class comment). A stale call racing a routing
    // change mid-tick can only mismatch `snapshot->positions` against
    // `plan` for one tick -- updateMembership's own bounds handle that
    // without reading past either span (RoutingMatrix.h's own comment).
    if (const auto snapshot = analysisThread_.latest()) {
        rail_.updateMembership(currentRoutingPlan(), snapshot->positions);
    }
}

rta::measure::RoutingPlan MainComponent::currentRoutingPlan() const {
    // The channel COUNT must be the bus's, exactly as AnalysisThread's own
    // planRouting calls use (AnalysisThread.cpp): `snapshot->positions[i]`
    // describes THAT plan's route i, and updateMembership zips the two by
    // index. Passing kMaxTransferFunctions here (a transfer-function cap, not
    // a channel count -- an index from one table used in another, memory
    // an-index-from-one-table-used-in-another) dropped every measurement
    // channel past 7 from THIS plan while the analysis still routed it,
    // shifting every later row's AVG text onto the wrong channel.
    return rta::measure::planRouting(audioIo_.bus().config(), audioIo_.bus().numChannels());
}

// paint() / resized(): MainComponentLayout.cpp (this file's own 400-line-cap
// split, fix round PRs #36/#37 item 3).
