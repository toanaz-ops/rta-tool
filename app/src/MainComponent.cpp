// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.6 (Wave E / T10).
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
      devicePanel_(audioIo_),
      channelRoleTable_(audioIo_.bus().config()),
      routingMatrix_(audioIo_.bus().config(), rta::measure::kMaxTransferFunctions),
      // The default workspace when none has been loaded: exactly one `rta`
      // pane, so the app's opening screen stays byte-for-byte what it was
      // before this task (task brief, step 3). Nothing in this class loads
      // a session yet. The pane selector (`selectPaneView`) can rebuild this
      // to a different single pane; the shape it starts in is unchanged.
      workspace_(std::make_unique<rta::view::WorkspaceView>(
          std::vector<rta::trace::PaneSpec>{rta::trace::PaneSpec{}}, makePaneFactory(analysisThread_))) {
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

    // Fix round item 4: these three are children of railScrollContent_, not
    // of this class -- railScrollView_ is what actually sits in the rail
    // (MainComponentLayout.cpp's own comment on why).
    railScrollContent_.addAndMakeVisible(devicePanel_);
    railScrollContent_.addAndMakeVisible(channelRoleTable_);
    railScrollContent_.addAndMakeVisible(routingMatrix_);
    railScrollView_.setViewedComponent(&railScrollContent_, false);
    addAndMakeVisible(railScrollView_);

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
        devicePanel_.setEnabled(false);

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
        channelRoleTable_.setChannelNames(lastChannelNames_);
        // routingMatrix_ caches cell TEXT rather than reading config_ live
        // at paint time the way channelRoleTable_'s ListBox does
        // (RoutingMatrix.h's own class comment: refreshFromConfig() is
        // "production API" precisely because nothing calls it
        // automatically) -- without this, the two role assignments just
        // above would show as UNUSED here until the next timerCallback tick
        // or an operator's own click.
        routingMatrix_.refreshFromConfig();
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

        devicePanel_.setEnabled(true);
        lastChannelNames_.clear();
        refreshChannelNamesFromDevice();
        routingMatrix_.refreshFromConfig();
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

void MainComponent::timerCallback() {
    // routingMatrix_ caches its cell text (RoutingMatrix.h's own class
    // comment) rather than reading rta::platform::ChannelConfig live at
    // paint time, so it needs an explicit poke to notice a role change made
    // anywhere OTHER than its own click -- channelRoleTable_'s clicks in
    // LIVE mode, chiefly. Cheap: kMaxTransferFunctions (8) cells, twice a
    // second.
    routingMatrix_.refreshFromConfig();
    refreshMembershipFromSnapshot();
    pollLocatePipeline();
    pollCalibrationPipeline();
    pollSplLogging();

    if (isSyntheticMode()) {
        return;  // fixed list, set once in setSyntheticMode()
    }
    refreshChannelNamesFromDevice();
}

void MainComponent::refreshChannelNamesFromDevice() {
    auto names = audioIo_.currentState().inputChannelNames;
    if (names != lastChannelNames_) {
        lastChannelNames_ = names;
        channelRoleTable_.setChannelNames(lastChannelNames_);
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
        const auto plan =
            rta::measure::planRouting(audioIo_.bus().config(), rta::measure::kMaxTransferFunctions);
        routingMatrix_.updateMembership(plan, snapshot->positions);
    }
}

// paint() / resized(): MainComponentLayout.cpp (this file's own 400-line-cap
// split, fix round PRs #36/#37 item 3).
