// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.6 (Wave E / T10) and §9
// trap T-1.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <az_ui/az_ui.h>

// ApiServer.h is a pimpl and pulls in no server library, which is the reason
// this composition root can name it at all (lane L-API Task I, D2).
#include "api/ApiServer.h"
#include "api/ApiSettings.h"
#include "measure/AnalysisThread.h"
#include "measure/Analyser.h"
#include "measure/CalibrationSession.h"
#include "measure/SyntheticInput.h"
#include "rta/dsp/DelayPolicy.h"
#include "rta/platform/AudioIo.h"
#include "trace/TraceLibrary.h"
#include "view/ChannelRoleTable.h"
#include "view/DevicePanel.h"
#include "view/RoutingMatrix.h"
#include "view/WorkspaceView.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "PaneFactory.h"

/// The real measurement window's contents: composition root for one screen,
/// not a DSP class and not a drawing class beyond its own masthead and
/// background. Left rail carries a LIVE/SYNTHETIC mode switch, the device
/// panel and the channel role table; the rest of the window is `workspace_`,
/// a 1..3 pane stack fed by `analysisThread_` regardless of which source is
/// currently writing into the bus underneath it, plus `library_`, the
/// stored-trace library every pane in it can draw from (task 10: the seam
/// that stayed unreached for a whole session -- docs/HANDOFF.md).
///
/// ## Trap T-1: member declaration order is load-bearing
///
/// `audioIo_` is declared BEFORE `analysisThread_` and `syntheticInput_`.
/// Members are destroyed in REVERSE declaration order, so both threads --
/// which keep reading `audioIo_.bus()` until their own destructors return --
/// are torn down before the bus (and the device feeding it) dies. Both
/// thread classes also call `stopThread()` in their own destructors as a
/// second, independent guarantee of the same thing (belt and braces, per the
/// plan): getting this wrong is a crash that happens only at shutdown, on a
/// customer's machine, once.
///
/// The same rule extends to `library_` and `workspace_`: `workspace_` holds
/// a raw, non-owning pointer into `library_` (handed over by `setLibrary`
/// below), so `library_` must be declared BEFORE `workspace_`. Declared
/// earlier means destroyed LATER (reverse declaration order again):
/// `workspace_` -- and every child pane torn down inside it -- unwinds
/// first, while `library_` is still alive, and only then does `library_`
/// itself go. Reversing the two would leave a child pane's destructor
/// holding a pointer into an already-destroyed library.
///
/// ## One bus, one reader, two possible writers
///
/// `analysisThread_` is constructed once, against `audioIo_.bus()`, and
/// never re-pointed: it reads whatever the bus's rings hold regardless of
/// whether `audioIo_`'s real device callback or `syntheticInput_`'s thread
/// is the one filling them. `setSyntheticMode()` is what makes sure exactly
/// one of those two is ever active at a time -- `CaptureBus::prepare()`'s
/// own precondition is that nothing else is concurrently writing.
class MainComponent final : public juce::Component, private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /// Switches the analysis chain between `audioIo_`'s live device and a
    /// deterministic `SyntheticInput` feed into the SAME bus (plan §0 item
    /// 4: the whole chain must be drivable with no hardware at all). Exposed
    /// as a plain method, not only reachable through the switch's own click
    /// handler, so the offscreen snapshot harness (tools/snapshot.cpp) can
    /// drive the exact seam a user's click uses rather than a second,
    /// divergent path. Idempotent: switching to the mode already active does
    /// nothing.
    void setSyntheticMode(bool enabled);

    [[nodiscard]] bool isSyntheticMode() const noexcept { return syntheticInput_ != nullptr; }

private:
    void timerCallback() override;
    void modeSwitchClicked();

    /// L7-DELAY task F2 (record docs/dsp/2026-09-06-l7-auto-delay.md
    /// sec.11.2-11.4). LOCATE: pink noise, strict solo on output channel 0
    /// (record sec.3, DEL-R4 -- a Locate is a measurement action), armSource;
    /// the settle wait and the capture itself run on `analysisThread_`
    /// (armLocateCapture) and are polled from `pollLocatePipeline()`, not
    /// blocked on here. APPLY: rebuilds every live Analyser with the found
    /// delay (record sec.1.6, sec.8 -- an explicit rebuild, never a live
    /// nudge), enabled only once a suggestion reads `Accepted`.
    void locateClicked();
    void applyClicked();

    /// Called every `timerCallback()` tick: advances the Locate state
    /// machine (settle wait -> arm the accumulator -> read the finished
    /// capture -> suggestDelay -> disarm) without blocking the message
    /// thread on any step of it.
    void pollLocatePipeline();
    void updateDelayReadout();

    /// L6a Wave 3 (record §8): the calibration flow's UI half, defined in
    /// MainComponentCalibration.cpp. Reuses AnalysisThread's raw-capture
    /// accumulator rather than a second bus reader -- route 0's measurement
    /// channel, raw, is what a calibrator on that mic delivers.
    void calibrationStartClicked();
    void calibrationEndClicked();
    void pollCalibrationPipeline();
    void updateCalibrationReadout();

    /// Task W2-E2b part A (record §8, §10 C4: "a weighting change starts a
    /// new log"; by the same reasoning, so does a calibration change).
    /// Called from `pollCalibrationPipeline()` right after a START check
    /// completes: stops any log already running and starts a fresh one whose
    /// `SplConfig::referenceOffsetDb`/`calibrated` carry the new offset --
    /// never applied to blocks the OLD, uncalibrated log already wrote. A
    /// no-op if nothing was logging yet (calibrating with no active
    /// measurement session has nothing to restart into).
    void restartSplLoggingForCalibration();

    /// Task W2-E2b part A. Called from `pollCalibrationPipeline()` right
    /// after an END check completes: writes the calibration record (start/end
    /// readings, drift, verdict, the block-index range the pair brackets --
    /// SplCalibrationRecord.h) into `currentSplSessionDir_`, and — when the
    /// drift exceeded ISO 1996-2:2017 cl. 5.2's 0.5 dB — marks this session's
    /// calibration invalid on `analysisThread_` so the live `SplBlockView`
    /// publishes it (record §15 A2). Runs on the message thread, never the
    /// analysis thread (task brief's own requirement). A no-op if
    /// `currentSplSessionDir_` is empty.
    void writeCalibrationRecordAndUpdateInvalidFlag();

    /// L6a task W2-E2a (record §10, §13 Q7), defined in MainComponentSpl.cpp:
    /// the composition root's own SPL wiring. Polled from `timerCallback()`
    /// -- the same shape `refreshChannelNamesFromDevice()` already uses --
    /// because neither a real device opening/closing nor `setSyntheticMode`
    /// posts an event this class can subscribe to; edge-detects on
    /// `splLoggingActive_` and calls `enableSplLogging`/`disableSplLogging`
    /// only on the transition, never every tick.
    void pollSplLogging();

    /// The half of `pollSplLogging()`'s EnableFresh action that is common to
    /// both an off-to-on edge and an epoch change while already active:
    /// reads the current Measurement-role channels, mints a fresh UTC
    /// session folder and calls `enableSplLogging`. Never called with the
    /// previous session still open -- the caller disables first when one
    /// was running (station-4 fix round, PR #31, verifier finding 1).
    ///
    /// `epoch` (station-4 fix round, round 3, LOW finding 3) is suffixed
    /// onto the folder name: the UTC timestamp alone has 1-second
    /// resolution, so two EnableFresh actions inside the same second (a
    /// rapid device bounce) would otherwise both resolve to the SAME folder,
    /// and the second one's createDirectory() + fresh log files would
    /// truncate the first session's still-unread ones. `epoch` only ever
    /// increases (CaptureBus::prepare()'s own fetch_add), so it makes the
    /// name unique by construction with no clock precision to lose.
    void startFreshSplLog(std::uint64_t epoch);

    /// The general form `startFreshSplLog(epoch)` calls with `SplConfig{}`
    /// (uncalibrated defaults) -- task W2-E2b part A's own seam, so a
    /// calibration-triggered restart (`restartSplLoggingForCalibration`) and
    /// a device/epoch-triggered one (`startFreshSplLog`) share every line
    /// except which config and calibrator level they pass down. Records the
    /// new folder into `currentSplSessionDir_` and the channel list into
    /// `currentSplLoggedChannels_` -- both read back by
    /// `writeCalibrationRecordAndUpdateInvalidFlag()` and
    /// `exportReportClicked()`.
    void startFreshSplLogWithConfig(const rta::measure::SplConfig& config,
                                    std::optional<double> calibratorLevelDb, std::uint64_t epoch);

    /// Task W2-E2b part B (plan Wave 4a-A / W2-E2b): reachable from a
    /// toolbar button. Builds a `ReportPayload` from `currentSplSessionDir_`
    /// (SplReportPayloadBuilder.h) -- reading Ln/dose/alarm state from the
    /// most recently published live `Snapshot::spl`, since neither is in the
    /// log -- renders it (SplReport.h, already shipped) and writes
    /// `report.html` into that same folder. Message-thread file I/O, a
    /// one-off user action, never the analysis thread. A no-op if nothing has
    /// logged a block yet.
    void exportReportClicked();

    /// Re-reads `audioIo_.currentState().inputChannelNames` and pushes it
    /// into `channelRoleTable_` only when it actually changed -- called from
    /// the poll timer while in LIVE mode (a device can be opened, closed or
    /// swapped from `devicePanel_` at any time, entirely outside this
    /// class's own control flow) and once, directly, when leaving SYNTHETIC
    /// mode.
    void refreshChannelNamesFromDevice();

    /// Station-4 fix F3 (record §6): pushes the AVG column from the latest
    /// published Snapshot's `positions`. Called from BOTH `timerCallback()`
    /// (2 Hz, live/interactive use) AND `resized()` -- NOT the timer alone,
    /// because `juce::Timer` fires through the message loop, and
    /// tools/snapshot.cpp's offscreen render drives this class through a
    /// blocking `juce::Thread::sleep()` with no message loop pumped at all,
    /// the same reason `renderComponent()` there has to call `resized()`
    /// explicitly (that file's own comment on trap T-6). Without this second
    /// call site, main-live.png would show every row's ROLE (set eagerly by
    /// `setSyntheticMode()`) but every row's AVG stuck at its
    /// construction-time "--", never having had a chance to run.
    void refreshMembershipFromSnapshot();

    // --- Trap T-1: declaration order is load-bearing. See the class comment.
    rta::platform::AudioIo audioIo_;
    rta::measure::AnalysisThread analysisThread_;
    std::unique_ptr<rta::measure::SyntheticInput> syntheticInput_;
    /// Lane L-API Task J, and trap T-1's third case. `apiServer_` holds
    /// `analysisThread_` as a `SnapshotSource&`, so it is declared AFTER it
    /// and is therefore destroyed BEFORE it -- the server stops and joins
    /// while the source it reads is still alive. Reversing the two is a
    /// shutdown crash nobody sees in development.
    ///
    /// A `unique_ptr` rather than a by-value member for one reason: the API
    /// is off by default (record sec.8), and a null pointer is the honest
    /// spelling of "the operator did not ask for this". `ApiServer` itself
    /// also starts nothing when disabled, so both halves say it.
    std::unique_ptr<rta::api::ApiServer> apiServer_;
    // -------------------------------------------------------------------

    juce::TextButton modeSwitch_{"SYNTHETIC"};

    // --- L7-DELAY task F2: Locate / Apply ----------------------------------
    juce::TextButton locateButton_{"LOCATE"};
    juce::TextButton applyButton_{"APPLY"};
    juce::Label delayReadout_;

    bool locateWaitingForSettle_ = false;
    bool locateCaptureArmed_ = false;
    std::shared_ptr<const rta::measure::LocateCapture> lastHandledCapture_;
    std::optional<rta::dsp::DelaySuggestion> delaySuggestion_;
    // ------------------------------------------------------------------------

    // --- L6a Wave 3: the calibration flow --------------------------------
    /// Route 0's measurement channel, raw -- what a calibrator clipped onto
    /// the mic capsule delivers (MainComponentCalibration.cpp's own original
    /// comment). Hoisted to a class constant (task W2-E2b part A) so it is
    /// one spelling shared by the calibration capture itself, the block-index
    /// range a calibration record brackets, and the channel
    /// `writeCalibrationRecordAndUpdateInvalidFlag()` marks invalid --
    /// previously a `MainComponentCalibration.cpp`-local anonymous-namespace
    /// constant that only that file could see.
    static constexpr int kCalibrationRouteIndex = 0;

    juce::TextButton calibrationStartButton_{"CAL START"};
    juce::TextButton calibrationEndButton_{"CAL END"};
    juce::Label calibrationReadout_;
    rta::measure::CalibrationSession calibrationSession_;
    /// Guards the one shared capture accumulator against Locate and
    /// Calibration both arming it at once.
    bool calibrationCaptureArmed_ = false;
    bool calibrationCaptureIsStart_ = false;
    /// When the currently-armed capture was requested, read from
    /// `juce::Time::getMillisecondCounterHiRes()` -- a MONOTONIC counter,
    /// not wall time (W2-E2a fix: PR #27 round-2 verifier, LOW). `double`
    /// because that is what the monotonic counter itself returns.
    /// `captureTimedOut` (fix round finding 6) is what stops a
    /// calibrator-only rig's missing REF channel from locking Locate out for
    /// the rest of the session.
    double calibrationCaptureArmedAtMs_ = 0.0;
    std::shared_ptr<const rta::measure::LocateCapture> lastHandledCalibrationCapture_;
    // ----------------------------------------------------------------------

    // --- L6a task W2-E2a: SPL logging follows the bus, not a button --------
    // What `pollSplLogging()` last told `analysisThread_` -- the previous
    // tick's `isSyntheticMode() || audioIo_.isRunning()`, so that function
    // can call `enableSplLogging`/`disableSplLogging` only on the transition
    // rather than once per tick.
    bool splLoggingActive_ = false;
    /// Station-4 fix round (PR #31, verifier finding 1, HIGH): `audioIo_.
    /// bus().epoch()` as of the last tick `pollSplLogging()` acted on. A
    /// sample-rate or device-list change restarts the device WITHOUT ever
    /// clearing `AudioIo::isRunning()` (platform/src/AudioIo.cpp,
    /// AudioIo_Devices.cpp), so `splLoggingActive_` alone cannot see a
    /// mid-session reconfiguration -- the epoch, which
    /// `AnalysisThread::rebuildAnalysersIfEpochChanged` already relies on
    /// for the same reason, can. See measure/SplLoggingDecision.h.
    std::uint64_t lastSplEpoch_ = 0;
    /// Task W2-E2b: the folder `startFreshSplLog`/`startFreshSplLogWithConfig`
    /// most recently created -- where a calibration record
    /// (`writeCalibrationRecordAndUpdateInvalidFlag`) and `report.html`
    /// (`exportReportClicked`) are written. Empty until the first log opens.
    std::string currentSplSessionDir_;
    /// The channel list that folder's log(s) were opened for -- read back by
    /// `exportReportClicked()` so it asks the payload builder for exactly the
    /// channels that are actually logging, not a hardcoded one.
    std::vector<int> currentSplLoggedChannels_;
    // ----------------------------------------------------------------------

    // --- L6a task W2-E2b part B: export the report from a live session -----
    juce::TextButton exportReportButton_{"EXPORT REPORT"};
    juce::Label exportReportReadout_;
    // ----------------------------------------------------------------------

    rta::view::DevicePanel devicePanel_;
    rta::view::ChannelRoleTable channelRoleTable_;
    /// Task F2 (record §6, §7): assigns Measurement/Reference roles and a
    /// transfer-function index per channel (`ChannelConfig::setRole` /
    /// `setTransferFunction`, task B1) -- the two facts `planRouting`
    /// resolves into the routes `AnalysisThread` averages. Fixed at
    /// `rta::measure::kMaxTransferFunctions` rows (RoutingMatrix.h has no
    /// dynamic resize API, task B7's own limit): routing more channels than
    /// this app can hold live `Analyser`s for has no route to assign them
    /// to anyway.
    rta::view::RoutingMatrix routingMatrix_;

    // --- library_ before workspace_ is load-bearing too. See the class
    // comment's extension of trap T-1.
    rta::trace::TraceLibrary library_;
    rta::view::WorkspaceView workspace_;
    // -------------------------------------------------------------------

    juce::Rectangle<int> mastheadArea_;

    // What channelRoleTable_ was last told, so refreshChannelNamesFromDevice
    // only pushes a new list (which resets ListBox scroll/selection) when the
    // device's own channel names actually changed.
    std::vector<std::string> lastChannelNames_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
