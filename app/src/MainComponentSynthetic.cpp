// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. MainComponent::setSyntheticMode, split out of
// MainComponent.cpp for its 400-line cap (same shape as MainComponentDelay.cpp).
// The class design rationale lives at the top of MainComponent.cpp.
#include "MainComponent.h"

#include "measure/SyntheticCrossover.h"
#include "rta/dsp/ButterworthDesign.h"

namespace {

// SyntheticInput has no device of its own to name channels after (a device
// panel is not involved), so this class names them itself. Two names, not
// one per role: SyntheticInput writes two channels regardless of which
// role(s) the table below assigns to them.
const std::vector<std::string> kSyntheticChannelNames{"Synthetic L", "Synthetic R"};

}  // namespace

/// Switches the analysis chain between `audioIo_`'s live device and a
/// deterministic `SyntheticInput` feed into the SAME bus (plan §0 item
/// 4: the whole chain must be drivable with no hardware at all). Exposed
/// as a plain method, not only reachable through the switch's own click
/// handler, so the offscreen snapshot harness (tools/snapshot.cpp) can
/// drive the exact seam a user's click uses rather than a second,
/// divergent path. Idempotent: switching to the mode already active does
/// nothing.
void MainComponent::setSyntheticMode(bool enabled) {
    if (enabled == isSyntheticMode()) {
        return;
    }

    // A LOCATE armed in the old mode would otherwise keep its settle/armed
    // flags for good: its settle clock (renderedSamples) and capture come from
    // a source that no longer runs, and locateClicked() refuses while either
    // flag is set -- LOCATE stayed dead until a device restart.
    cancelLocate();

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
        // Sweep-IR lane decision 8: a 40 Hz - 16 kHz band-pass on the
        // MEASUREMENT copy, so the synthetic transfer curve has checkable
        // -3 dB edges instead of a flat line. The Reference stays unfiltered.
        // Two SOS sections, not the plan's one: with N = 1 the band-pass has
        // two REAL poles and `pairIntoSections` throws (it requires complex
        // pole pairs), which killed setSyntheticMode itself.
        syntheticConfig.measurementFilter =
            rta::dsp::ButterworthDesign::bandPass(40.0, 16000.0, syntheticConfig.sampleRate, 2)
                .sections;
        // ALIGN-UI decision 10: output 1 (the sub) has its own path -- LR4
        // low-pass at 100 Hz, delay 100 -- so the default ALIGN run has an
        // answer (the sub arrives 96 samples after output 0's 4). Output 0
        // above is untouched.
        syntheticConfig.secondOutputDelaySamples = 100;
        syntheticConfig.secondOutputFilter =
            rta::measure::lr4LowPass(100.0, syntheticConfig.sampleRate);
        // Loopback: LOCATE, SWEEP and VERIFY play through the output engine;
        // the device that would normally render it is stopped here, so the
        // synthetic thread renders it and carries the result onto the bus.
        syntheticConfig.loopback = &audioIo_.output();
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
