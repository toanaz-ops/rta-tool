// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.6 (Wave E / T10).
#include "MainComponent.h"

#include "trace/Workspace.h"
#include "view/PaneRegistry.h"
#include "view/RtaView.h"
#include "view/TransferView.h"

namespace {

// The legend gutter's own column, per Metrics.h: every panel that shares a
// legend column starts its fields at az::ui::gutterWidth, but that figure
// sizes a FIELD's legend, not a whole rail's width. This is the rail width
// itself -- wide enough for DevicePanel's widest row (a device name combo
// plus its gutter) and for ChannelRoleTable's index/name/role columns
// without either one clipping.
constexpr int kRailWidth = 360;

// Tall enough for DevicePanel's own content (caption, four field rows, the
// start/stop switch and the fault line -- see DevicePanel::resized()) with a
// little breathing room below rather than a height computed to the pixel:
// the panel fills whatever it is given and a hairline of empty panel face at
// the bottom costs nothing.
constexpr int kDevicePanelHeight = 300;

// Task F2: az::ui::GridPanel divides whatever bounds it is given across its
// header row plus rta::measure::kMaxTransferFunctions (8) data rows -- it
// has no minimum-row-height floor of its own (GridPanel.h's own class
// comment: "geometry only"), so this is a flat pixel budget rather than a
// per-row metric multiplied out: comfortably readable for 9 rows (header +
// 8 channels) without crowding channelRoleTable_ below it out of the rail.
constexpr int kRoutingMatrixHeight = 220;

// SyntheticInput has no device of its own to name channels after (a device
// panel is not involved), so this class names them itself. Two names, not
// one per role: SyntheticInput writes two channels regardless of which
// role(s) the table below assigns to them.
const std::vector<std::string> kSyntheticChannelNames{"Synthetic L", "Synthetic R"};

// The pane factory `workspace_` is built with -- the one place in the whole
// lane that names both `RtaView` and `TransferView` alongside the
// `AnalysisThread` reference they both read from, because that wiring is
// this composition root's job and no one else's (WorkspaceView.h's own
// class comment: including both pane headers there would make it a second
// composition root). `source` is captured by reference, not copied -- both
// pane constructors already take `const SnapshotSource&` and hold onto that
// reference themselves (RtaView.h, TransferView.h), so this factory outlives
// nothing they do not already outlive.
rta::view::WorkspaceView::PaneFactory makePaneFactory(rta::measure::SnapshotSource& source) {
    return [&source](rta::view::PaneView view) -> std::unique_ptr<juce::Component> {
        if (view == rta::view::PaneView::Transfer) {
            return std::make_unique<rta::view::TransferView>(source);
        }
        return std::make_unique<rta::view::RtaView>(source);
    };
}

}  // namespace

MainComponent::MainComponent()
    : analysisThread_(audioIo_.bus(), rta::measure::Analyser::Config{}),
      devicePanel_(audioIo_),
      channelRoleTable_(audioIo_.bus().config()),
      routingMatrix_(audioIo_.bus().config(), rta::measure::kMaxTransferFunctions),
      // The default workspace when none has been loaded: exactly one `rta`
      // pane, so the app's opening screen stays byte-for-byte what it was
      // before this task (task brief, step 3). Nothing in this class loads
      // a session yet -- that is a different seam -- so this is the only
      // workspace shape MainComponent ever builds today.
      workspace_({rta::trace::PaneSpec{}}, makePaneFactory(analysisThread_)) {
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

    addAndMakeVisible(devicePanel_);
    addAndMakeVisible(channelRoleTable_);
    addAndMakeVisible(routingMatrix_);

    addAndMakeVisible(workspace_);
    // The seam this whole task exists for (docs/HANDOFF.md): a library was
    // built in L5a, a view could draw one, and nothing ever called this.
    workspace_.setLibrary(&library_);

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

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(az::ui::background);

    // Same two-piece masthead SpecimenComponent draws: the wordmark and its
    // qualifier want different weights, and a one-piece title needs a
    // non-ASCII dash, which is a source-encoding question this project has
    // no reason to open.
    auto masthead = mastheadArea_;
    auto rule = masthead.removeFromBottom(2);

    const auto brandFont = az::ui::legendFont(az::ui::switchFontSize, true, az::ui::trackingCaption);
    g.setColour(az::ui::text);
    g.setFont(brandFont);
    g.drawText("RTA TOOL", masthead, juce::Justification::centredLeft);

    masthead.removeFromLeft(static_cast<int>(az::ui::stringWidth(brandFont, "RTA TOOL")) + az::ui::gap * 3);
    g.setColour(az::ui::dim);
    g.setFont(az::ui::legendFont(az::ui::columnFontSize, false, az::ui::trackingColumn));
    g.drawText("REAL-TIME ANALYSER", masthead, juce::Justification::centredLeft);

    az::ui::drawEngravedDivider(g, rule);
}

void MainComponent::resized() {
    // Station-4 fix F3: also called here, not only from the 2 Hz timer --
    // see refreshMembershipFromSnapshot()'s own comment for why a caller
    // driving this class with no message loop pumped (tools/snapshot.cpp)
    // would otherwise never see the AVG column populated at all.
    refreshMembershipFromSnapshot();

    auto area = getLocalBounds().reduced(az::ui::gap * 2);

    mastheadArea_ = area.removeFromTop(az::ui::transportHeight / 2);
    area.removeFromTop(az::ui::gap * 2);

    auto rail = area.removeFromLeft(kRailWidth);
    area.removeFromLeft(az::ui::gap * 2);

    modeSwitch_.setBounds(rail.removeFromTop(az::ui::buttonCellHeight));
    rail.removeFromTop(az::ui::gap * 2);

    // L7-DELAY task F2: one fixed row -- LOCATE, APPLY, and the readout
    // sharing what's left of the row's width.
    auto locateRow = rail.removeFromTop(az::ui::buttonCellHeight);
    const int buttonWidth = (locateRow.getWidth() - az::ui::gap * 2) / 3;
    locateButton_.setBounds(locateRow.removeFromLeft(buttonWidth));
    locateRow.removeFromLeft(az::ui::gap);
    applyButton_.setBounds(locateRow.removeFromLeft(buttonWidth));
    locateRow.removeFromLeft(az::ui::gap);
    delayReadout_.setBounds(locateRow);
    rail.removeFromTop(az::ui::gap * 2);

    devicePanel_.setBounds(rail.removeFromTop(kDevicePanelHeight));
    rail.removeFromTop(az::ui::gap * 2);

    // Fixed height for kMaxTransferFunctions rows plus a header row --
    // RoutingMatrix has no dynamic resize the way channelRoleTable_'s
    // ListBox does, so it gets a fixed slice rather than "whatever is left".
    routingMatrix_.setBounds(rail.removeFromTop(kRoutingMatrixHeight));
    rail.removeFromTop(az::ui::gap * 2);

    // Fills whatever is left of the rail -- a shrunk window trims rows off
    // the bottom of the channel list before it ever touches the plot.
    channelRoleTable_.setBounds(rail);

    workspace_.setBounds(area);
}
