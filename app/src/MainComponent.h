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
#include "rta/platform/AudioIo.h"
#include "view/ChannelRoleTable.h"
#include "view/DevicePanel.h"
#include "view/RtaView.h"

#include <memory>
#include <string>
#include <vector>

/// The real measurement window's contents: composition root for one screen,
/// not a DSP class and not a drawing class beyond its own masthead and
/// background. Left rail carries a LIVE/SYNTHETIC mode switch, the device
/// panel and the channel role table; the rest of the window is the band
/// plot, fed by `analysisThread_` regardless of which source is currently
/// writing into the bus underneath it.
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

    /// Re-reads `audioIo_.currentState().inputChannelNames` and pushes it
    /// into `channelRoleTable_` only when it actually changed -- called from
    /// the poll timer while in LIVE mode (a device can be opened, closed or
    /// swapped from `devicePanel_` at any time, entirely outside this
    /// class's own control flow) and once, directly, when leaving SYNTHETIC
    /// mode.
    void refreshChannelNamesFromDevice();

    // --- Trap T-1: declaration order is load-bearing. See the class comment.
    rta::platform::AudioIo audioIo_;
    rta::measure::AnalysisThread analysisThread_;
    std::unique_ptr<rta::measure::SyntheticInput> syntheticInput_;
    // -------------------------------------------------------------------

    juce::TextButton modeSwitch_{"SYNTHETIC"};
    rta::view::DevicePanel devicePanel_;
    rta::view::ChannelRoleTable channelRoleTable_;
    rta::view::RtaView rtaView_;

    juce::Rectangle<int> mastheadArea_;

    // What channelRoleTable_ was last told, so refreshChannelNamesFromDevice
    // only pushes a new list (which resets ListBox scroll/selection) when the
    // device's own channel names actually changed.
    std::vector<std::string> lastChannelNames_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
