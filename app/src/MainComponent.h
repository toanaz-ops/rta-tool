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
#include "MainComponentAlign.h"
#include "MainComponentEq.h"
#include "MainComponentGenerator.h"
#include "MainComponentPaneSelector.h"
#include "MainComponentRail.h"
#include "MainComponentRt60.h"
#include "MainComponentSession.h"
#include "MainComponentSweep.h"
#include "measure/AnalysisThread.h"
#include "measure/Analyser.h"
#include "measure/CalibrationSession.h"
#include "measure/SyntheticInput.h"
#include "rta/dsp/DelayPolicy.h"
#include "rta/platform/AudioIo.h"
#include "trace/TraceLibrary.h"
#include "view/PaneSelectorDecision.h"
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
/// a 1..3 pane stack fed by `analysisThread_`, plus `library_`, the
/// stored-trace library every pane in it can draw from.
///
/// Full design rationale -- including trap T-1, why `audioIo_` is declared
/// before `analysisThread_`/`syntheticInput_`/`apiServer_` and why `library_`
/// is declared before `workspace_` (both load-bearing for destruction
/// order) -- lives in MainComponent.cpp's own header comment (T0 file-length
/// split, this class stayed the same either way).
class MainComponent final : public juce::Component, private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Rationale: MainComponentSynthetic.cpp, above setSyntheticMode().
    void setSyntheticMode(bool enabled);

    [[nodiscard]] bool isSyntheticMode() const noexcept { return syntheticInput_ != nullptr; }

    // Rebuilds `workspace_` via the SAME `makePaneFactory` seam the
    // constructor uses (MainComponentPanes.cpp). Never touches SPL logging.
    void selectPaneView(rta::view::PaneSelectorButton button);

    [[nodiscard]] rta::view::PaneView currentPaneView() const noexcept { return currentPaneView_; }

private:
    friend struct MainComponentTestAccess;  // fix round item 2: only caller of the trio below
    // Test-only trio: the real pane makePaneFactory built, SPL logging, and the trace library panes should be wired to.
    [[nodiscard]] const juce::Component& paneComponentForTest() const { return *workspace_->getChildComponent(0); }
    [[nodiscard]] rta::measure::AnalysisThread& analysisThreadForTest() noexcept { return analysisThread_; }
    // Non-const (app/crossover-pane): a test needs TraceLibrary::add to seed
    // the XOVER pane's two stored traces.
    [[nodiscard]] rta::trace::TraceLibrary& libraryForTest() noexcept { return library_; }

    void timerCallback() override;
    void modeSwitchClicked();

    /// ONE answer to "may a new LOCATE / CAL / SWEEP / VERIFY / ALIGN take the
    /// output and the shared accumulator now?" (Z25). `captureBusy`: one of
    /// them holds the accumulator. `outputBusy`: that, or VERIFY is playing.
    [[nodiscard]] bool captureBusy() const noexcept {
        return locateWaitingForSettle_ || locateCaptureArmed_ || calibrationCaptureArmed_ || sweep_.busy() ||
               align_.busy();
    }
    /// H7: GEN joins `outputBusy()` (and VERIFY's env) but NOT `captureBusy()`.
    [[nodiscard]] bool outputBusy() const noexcept {
        return captureBusy() || eq_.verifyBusy() || generator_.active();
    }
    // MainComponentGeneratorWiring.cpp: what GEN may ask the app, and who else holds the output.
    [[nodiscard]] rta::measure::GeneratorEnvironment generatorEnvironment() const;
    [[nodiscard]] std::string outputHolderName() const;

    rta::view::PaneResolution restoreWorkspaceFromSession(std::vector<rta::trace::PaneSpec> panes);  // rationale: MainComponentPanes.cpp

    // Rationale: MainComponentDelay.cpp, above each definition.
    void locateClicked();
    void applyClicked();
    void pollLocatePipeline();
    void cancelLocate();
    void updateDelayReadout();

    // Rationale: MainComponentCalibration.cpp, above each definition.
    void calibrationStartClicked();
    void calibrationEndClicked();
    void pollCalibrationPipeline();
    void updateCalibrationReadout();
    void restartSplLoggingForCalibration();
    void writeCalibrationRecordAndUpdateInvalidFlag();

    // Rationale: MainComponentSpl.cpp, above each definition.
    void pollSplLogging();
    void startFreshSplLog(std::uint64_t epoch);
    void startFreshSplLogWithConfig(const rta::measure::SplConfig& config,
                                    std::optional<double> calibratorLevelDb, std::uint64_t epoch);
    void exportReportClicked();
    void storeClicked();  // MainComponentStore.cpp (station-3 T5): freezes the pane's Snapshot into library_
    /// STORE's conversion, shared with VERIFY's after-measurement (MainComponentStore.cpp).
    [[nodiscard]] std::optional<rta::trace::Trace> freezeSnapshot(const rta::measure::Snapshot& snapshot,
                                                                  rta::view::PaneView pane);
    void wireEqVerify();  // rationale: MainComponentEqVerify.cpp
    void wireAlign();  // MainComponentAlign.cpp: device, snapshot and busy flags for MEASURE/SUM
    void wireRt60();   // MainComponentRt60.cpp: the SWEEP half's IR store, readout and a tagged press

    // Rationale: MainComponent.cpp, above each definition.
    void refreshChannelNamesFromDevice();
    void applyChannelNames(std::vector<std::string> names);
    [[nodiscard]] rta::measure::RoutingPlan currentRoutingPlan() const;
    void refreshMembershipFromSnapshot();

    // --- Trap T-1: declaration order is load-bearing. See the class comment.
    rta::platform::AudioIo audioIo_;
    rta::measure::AnalysisThread analysisThread_;
    std::unique_ptr<rta::measure::SyntheticInput> syntheticInput_;
    std::unique_ptr<rta::api::ApiServer> apiServer_;  // rationale: MainComponent.cpp, above the constructor
    // -------------------------------------------------------------------

    juce::TextButton modeSwitch_{"SYNTHETIC"};

    // --- L7-DELAY task F2: Locate / Apply ----------------------------------
    juce::TextButton locateButton_{"LOCATE"};
    juce::TextButton applyButton_{"APPLY"};
    juce::Label delayReadout_;

    bool locateWaitingForSettle_ = false;
    bool locateCaptureArmed_ = false;
    std::uint64_t locateResync_ = 0;  // AnalysisThread::resyncSequence() when the capture was armed
    std::shared_ptr<const rta::measure::LocateCapture> lastHandledCapture_;
    std::optional<rta::dsp::DelaySuggestion> delaySuggestion_;
    // ------------------------------------------------------------------------

    // --- L6a Wave 3: the calibration flow --------------------------------
    // T0 file-length split (2026-09-29): the field-by-field rationale for
    // every member below (why each exists, what fix round it traces to, who
    // reads it) moved verbatim to MainComponentCalibration.cpp's own header
    // comment -- comment text only, unchanged in substance.
    static constexpr int kCalibrationRouteIndex = 0;

    juce::TextButton calibrationStartButton_{"CAL START"};
    juce::TextButton calibrationEndButton_{"CAL END"};
    juce::Label calibrationReadout_;
    rta::measure::CalibrationSession calibrationSession_;
    bool calibrationCaptureArmed_ = false;
    bool calibrationCaptureIsStart_ = false;
    double calibrationCaptureArmedAtMs_ = 0.0;
    std::shared_ptr<const rta::measure::LocateCapture> lastHandledCalibrationCapture_;
    int calibrationChannel_ = -1;
    int calibrationStartChannel_ = -1;
    bool calibrationChannelRefused_ = false;
    // ----------------------------------------------------------------------

    // --- L6a task W2-E2a: SPL logging follows the bus, not a button --------
    // T0 file-length split (2026-09-29): the field-by-field rationale for
    // every member below moved verbatim to MainComponentSpl.cpp's own header
    // comment -- comment text only, unchanged in substance.
    bool splLoggingActive_ = false;
    std::uint64_t lastSplEpoch_ = 0;
    std::string currentSplSessionDir_;
    std::vector<int> currentSplLoggedChannels_;
    bool currentSplLogHasCalibratedOffset_ = false;
    // ----------------------------------------------------------------------

    // --- L6a task W2-E2b part B: export the report from a live session -----
    juce::TextButton exportReportButton_{"EXPORT REPORT"};
    juce::Label exportReportReadout_;
    bool forceExportThrowForTest_ = false;
    // ----------------------------------------------------------------------
    // STORE (station-3): freezes the live measurement into TraceLibrary.
    juce::TextButton storeButton_{"STORE"};
    juce::Label storeReadout_;

    // Fix round LOW F3: device panel + routing matrix + channel role table +
    // their scroll viewport (item 4), one member instead of five -- see
    // MainComponentRail.h. RoutingMatrix rows follow the device's channel count.
    MainComponentRail rail_;

    // Pane selector: a radio group. Not persisted (SPL-R11) -- starts at Rta.
    MainComponentPaneSelector paneSelector_;
    rta::view::PaneView currentPaneView_ = rta::view::PaneView::Rta;

    // --- library_ before workspace_ is load-bearing too. See the class
    // comment's extension of trap T-1.
    rta::trace::TraceLibrary library_;
    // L7-EQ UI (MainComponentEq.h): owns the EQ model, so the committed filters
    // survive workspace_ rebuilding the pane. Before workspace_: every EqPaneView
    // holds a pointer into it, and members are destroyed in reverse order.
    MainComponentEq eq_;
    // Same ownership as eq_ (MainComponentAlign.h): declared before workspace_.
    MainComponentAlign align_;
    // Same ownership and the same reason (MainComponentRt60.h): before workspace_.
    MainComponentRt60 rt60_;
    // A pointer: selectPaneView rebuilds it wholesale (WorkspaceView's
    // factory is consumed once, in its own constructor).
    std::unique_ptr<rta::view::WorkspaceView> workspace_;
    // -------------------------------------------------------------------

    // Owns SAVE/OPEN SESSION + readout; needs library_ constructed first.
    MainComponentSession session_;
    // SWEEP (MainComponentSweep.h): holds references to audioIo_/analysisThread_/
    // library_, so it is declared after all three -- its worker joins first.
    MainComponentSweep sweep_;
    // H7 generator (MainComponentGenerator.h): after sweep_ -- its host callbacks read sweep_.busy().
    MainComponentGenerator generator_;

    juce::Rectangle<int> mastheadArea_;

    // What channelRoleTable_ was last told, so refreshChannelNamesFromDevice
    // only pushes a new list (which resets ListBox scroll/selection) when the
    // device's own channel names actually changed.
    std::vector<std::string> lastChannelNames_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
