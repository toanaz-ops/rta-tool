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
//      ("STORED (FIXED FFT)"), plus a clause naming exactly which TRANSFER
//      panes are showing MTW right now (fix round 1, HIGH F1 -- an operator
//      can flip any pane to FIXED with the shipped TransferSourceToggle, or
//      MTW can be unavailable outright with mtwEnabled=false/no reference
//      yet, so "screen shows MTW" is a per-pane fact read off the live
//      TransferView, never a constant).
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
#include "view/TransferView.h"

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

// Fix round 1, HIGH F1: the workspace can show TRANSFER at any child index
// (a multi-pane session is not required to put it first -- see
// MainComponentPanes.cpp's own restoreWorkspaceFromSession comment on why
// currentPaneView_ only tracks the FIRST pane), so this scans rather than
// assuming index 0. K8 (docs/HUMAN-QA-QUEUE.md, PR #51 round-2 R6): returns
// EVERY TRANSFER pane, in workspace order, not only the first match -- a
// multi-pane layout can show TRANSFER more than once, and the old
// first-match-wins scan left every pane after the first silently unreported
// in the STORE readout. Empty when the current workspace has no TRANSFER
// pane at all -- storeClicked() only calls this once it already knows
// `pane == PaneView::Transfer`, but an empty result is still handled rather
// than assumed unreachable.
std::vector<const rta::view::TransferView*> findTransferViews(const juce::Component& workspace) {
    std::vector<const rta::view::TransferView*> views;
    for (int i = 0; i < workspace.getNumChildComponents(); ++i) {
        if (auto* view = dynamic_cast<const rta::view::TransferView*>(workspace.getChildComponent(i))) {
            views.push_back(view);
        }
    }
    return views;
}

// Fix round 1, HIGH F1: names, in Magnitude/Phase/Coherence order, exactly
// the panes whose EFFECTIVE source (TransferView::effectiveSource --
// preference plus availability, the same fallback renderTo() draws with) is
// Mtw right now. Empty when none is -- the caller appends no clause at all
// in that case, rather than a clause naming nothing.
//
// K9 (docs/HUMAN-QA-QUEUE.md, PR #51 round-2 R7): takes the snapshot
// `storeClicked()` already froze at the top of the click, rather than
// calling `TransferView::effectiveSource(pane)` (which would re-fetch
// `source_->latest()` on its own) -- the readout must describe the SAME
// snapshot the stored trace was built from, not whatever published in the
// meantime.
std::string mtwPaneNames(const rta::view::TransferView& view, const rta::measure::Snapshot& snapshot) {
    using rta::view::TransferPane;
    using rta::view::TransferSource;
    static constexpr std::pair<TransferPane, const char*> kPanes[] = {
        {TransferPane::Magnitude, "MAG"},
        {TransferPane::Phase, "PHASE"},
        {TransferPane::Coherence, "COH"},
    };
    std::string out;
    for (const auto& [pane, label] : kPanes) {
        if (view.effectiveSource(pane, snapshot) != TransferSource::Mtw) continue;
        if (!out.empty()) out += ", ";
        out += label;
    }
    return out;
}

// K8: one clause per TRANSFER pane that has anything to report, prefixed
// with which pane it belongs to ONLY when there is more than one -- a
// single-TRANSFER-pane workspace (still the common case) keeps the exact
// wording station-3's own fix round 1 already shipped and tested.
std::string mtwSummary(const std::vector<const rta::view::TransferView*>& views,
                       const rta::measure::Snapshot& snapshot) {
    std::vector<std::string> clauses;
    for (std::size_t i = 0; i < views.size(); ++i) {
        auto names = mtwPaneNames(*views[i], snapshot);
        if (names.empty()) continue;
        clauses.push_back(views.size() > 1 ? "TRANSFER " + std::to_string(i + 1) + ": " + names
                                           : names);
    }
    std::string out;
    for (std::size_t i = 0; i < clauses.size(); ++i) {
        if (i > 0) out += "; ";
        out += clauses[i];
    }
    return out;
}

}  // namespace

std::optional<rta::trace::Trace> MainComponent::freezeSnapshot(const rta::measure::Snapshot& snapshot,
                                                               rta::view::PaneView pane) {
    const std::string deviceName =
        isSyntheticMode() ? std::string("SYNTHETIC") : audioIo_.currentState().deviceName;
    const std::string channelRoles = channelRolesSummary(audioIo_.bus().config());

    // Fix round 1, MEDIUM F2: `captureConfig()`, never `config()` -- see
    // AnalysisThread.h's own comment on why a reference into `baseConfig_`
    // is not safe for the message thread to hold.
    return rta::trace::traceFromSnapshot(snapshot, pane, analysisThread_.captureConfig(), deviceName,
                                         channelRoles);
}

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
    // freezing below: nothing is needed for a refusal this cheap to see coming.
    if (pane == rta::view::PaneView::Transfer && !snapshot->hasReference) {
        storeReadout_.setText("store: TRANSFER has no reference fed -- nothing to freeze",
                              juce::dontSendNotification);
        return;
    }

    auto trace = freezeSnapshot(*snapshot, pane);
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
        // Owner decision 1: this always freezes the fixed-FFT TransferBlock
        // (research C1) regardless of what is on screen -- state which
        // panes are showing MTW right now (fix round 1, HIGH F1), never a
        // constant: an operator can flip any pane to FIXED
        // (TransferSourceToggle), and MTW can simply be unavailable
        // (mtwEnabled=false, or no reference fed yet). K8/K9: every TRANSFER
        // pane is named, against the SAME `snapshot` frozen at the top of
        // this click -- never a second, independent `latest()` fetch.
        const auto transferViews = findTransferViews(*workspace_);
        const auto mtwPanes = mtwSummary(transferViews, *snapshot);
        if (!mtwPanes.empty()) {
            message += " -- screen shows MTW on " + juce::String(mtwPanes);
        }
    }
    storeReadout_.setText(message, juce::dontSendNotification);
}
