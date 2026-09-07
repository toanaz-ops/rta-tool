// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.6 (Wave E / T10) and §9
// trap T-1.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <az_ui/az_ui.h>

#include "measure/AnalysisThread.h"
#include "measure/Analyser.h"
#include "measure/SyntheticInput.h"
#include "rta/dsp/DelayPolicy.h"
#include "rta/platform/AudioIo.h"
#include "trace/TraceLibrary.h"
#include "view/ChannelRoleTable.h"
#include "view/DevicePanel.h"
#include "view/RoutingMatrix.h"
#include "view/WorkspaceView.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

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
