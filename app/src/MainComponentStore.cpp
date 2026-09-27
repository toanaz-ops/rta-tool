// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. MainComponent.cpp's own 400-line-cap split
// (station-3 STORE plan, docs/plans/2026-09-27-store-lane-plan.md task T5),
// same shape as MainComponentDelay.cpp/MainComponentCalibration.cpp/
// MainComponentSpl.cpp/MainComponentPanes.cpp: member-function definitions
// declared in MainComponent.h, no different in kind from anything else in
// that class.
//
// Owner decisions 2026-09-27 this file implements:
//   1. Freezes the FIXED-FFT engine always, and the readout says so
//      ("STORED (FIXED FFT)"), plus an extra clause on TRANSFER when the pane
//      is showing MTW (TransferView.h's own default) so "why doesn't this
//      match what I was looking at" never has to be debugged live.
//   2. One global button, branching on currentPaneView() -- see
//      CaptureConverter::traceFromSnapshot for the RTA/TRANSFER split itself.
//   3. No adjustable knobs added here or on Trace/TraceLibrary -- post-hoc
//      trims already live one layer up, in VirtualTrace.
//   4. Naming ("<PANE> @ HH:MM:SS") and grouping (= the pane label) below;
//      no cap, no keyboard shortcut (research C4).
//
// Real-time safety: runs entirely on the message thread, in response to a
// button click. Reads `analysisThread_.latest()` -- the same atomic-pointer-
// swap `SnapshotSource::latest()` every other message-thread reader in this
// class already uses (refreshMembershipFromSnapshot, exportReportClicked) --
// and touches nothing on the audio callback or the analysis thread.
#include "MainComponent.h"

#include "rta/platform/ChannelConfig.h"
#include "trace/CaptureConverter.h"

namespace {

// A compact "ch<N>=<Role>" summary of every non-Unused channel, ascending --
// CaptureMeta::channelRoles is free-text (research A3.3: nothing in the tree
// parses it back), so this exists only to make a saved/exported capture's
// routing readable, not to round-trip through any decoder.
std::string channelRolesSummary(const rta::platform::ChannelConfig& config) {
    std::string out;
    for (int ch = 0; ch < rta::platform::kMaxChannels; ++ch) {
        const auto role = config.role(ch);
        if (role == rta::platform::ChannelRole::Unused) continue;
        if (!out.empty()) out += ",";
        out += "ch" + std::to_string(ch) + "=" + std::string(rta::platform::toString(role));
    }
    return out;
}

}  // namespace

void MainComponent::storeClicked() {
    const auto snapshot = analysisThread_.latest();
    if (!snapshot) {
        storeReadout_.setText("store: no measurement published yet", juce::dontSendNotification);
        return;
    }

    const auto pane = currentPaneView();

    // The call site's OWN guard, independent of CaptureConverter's copy
    // (T4's internal check) -- the same "refuse at a label before doing any
    // work" convention exportReportClicked already uses. Checked before
    // building deviceName/channelRoles below: neither is needed for a
    // refusal this cheap to see coming.
    if (pane == rta::view::PaneView::Transfer && !snapshot->hasReference) {
        storeReadout_.setText("store: TRANSFER has no reference fed -- nothing to freeze",
                              juce::dontSendNotification);
        return;
    }

    const std::string deviceName =
        isSyntheticMode() ? std::string("SYNTHETIC") : audioIo_.currentState().deviceName;
    const std::string channelRoles = channelRolesSummary(audioIo_.bus().config());

    auto trace = rta::trace::traceFromSnapshot(*snapshot, pane, analysisThread_.config(), deviceName,
                                               channelRoles);
    if (!trace.has_value()) {
        // Unreachable from RTA/TRANSFER given the guard above and T6's own
        // enable/disable -- kept as a stated refusal (never a silent no-op,
        // never a dereference of an empty optional) for whichever future
        // pane or edge this function does not yet know about.
        storeReadout_.setText("store: nothing to freeze on this pane", juce::dontSendNotification);
        return;
    }

    // T7: the group is the SAME juce::String the pane selector button itself
    // shows -- never a second "RTA"/"TRANSFER" literal -- so a rename of
    // paneRtaButton_/paneTransferButton_ cannot silently diverge from the
    // group a stored trace lands in (T7's own mutant).
    const juce::String paneLabel =
        pane == rta::view::PaneView::Transfer ? paneTransferButton_.getButtonText() : paneRtaButton_.getButtonText();
    const juce::String name = paneLabel + " @ " + juce::Time::getCurrentTime().formatted("%H:%M:%S");

    const auto id = library_.add(std::move(*trace), name.toStdString(), paneLabel.toStdString());
    if (id.empty()) {
        // TraceLibrary::add refuses only a duplicate id -- T3's monotonic
        // counter is what makes this branch unreachable in practice; kept as
        // a stated refusal rather than a silent no-op if it is ever hit.
        storeReadout_.setText("store: failed (duplicate capture id)", juce::dontSendNotification);
        return;
    }

    juce::String message = juce::String("STORED (FIXED FFT): ") + name;
    if (pane == rta::view::PaneView::Transfer) {
        // Owner decision 1: TransferView defaults every pane to the MTW
        // curve (TransferView.h:132) while this always freezes the
        // fixed-FFT TransferBlock (research C1) -- state both, unconditionally,
        // whenever the pane being stored is TRANSFER.
        message += " -- screen shows MTW";
    }
    storeReadout_.setText(message, juce::dontSendNotification);
}
