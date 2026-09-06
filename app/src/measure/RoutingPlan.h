// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Record §6: N transfer functions behind one drain, each naming its own
// reference channel. Header-only and a pure function over `ChannelConfig`
// (not an `AnalysisThread` member) for the same reason `PairedDrain.h`'s
// `pairedHopCount` is free: the routing DECISION is testable with
// RTA_BUILD_APP=OFF, with no bus and no thread running.
#pragma once

#include "rta/platform/ChannelConfig.h"

#include <algorithm>
#include <vector>

namespace rta::measure {

/// The compile-time cap on live transfer functions (record §6, task B2's own
/// "not decided here" note): `Analyser` is neither movable nor copyable
/// (`DualFftEngine`/`MtwEngine` hold `RingBuffer`s with atomic members), so
/// `AnalysisThread::analysers_` is built once, at this size, and never resized.
/// It lives HERE rather than in `AnalysisThread.h` because it is the cap on how
/// many routes can have an `Analyser` at all -- a routing fact both the drain
/// (`AnalysisThread`) and the publish-membership sync (`AnalysisPublish`) must
/// agree on, and `AnalysisPublish` cannot include `AnalysisThread.h` (that
/// dependency points the other way). 8 positions cost 424 MB resident at the
/// MTW engine's defaults (record §6's own arithmetic,
/// `docs/research/2026-09-06-l6b-station1-research.md` Part C) -- a memory
/// policy, not a DSP decision, for the owner to move if 8 is ever not enough.
inline constexpr int kMaxTransferFunctions = 8;

/// One transfer function's two channels, resolved from `ChannelConfig`'s
/// per-channel role + tfIndex table (`ChannelConfig::channelsWithRole` /
/// `transferFunction`, task B1).
struct TransferRoute {
    int tfIndex = 0;
    int referenceChannel = -1;
    int measurementChannel = -1;
};

/// What one hop's drain needs to do: which pairs to feed which `Analyser`,
/// which reference channels to peek exactly once (record §6: "the drain
/// reads every distinct reference channel once per hop and hands each
/// Analyser the one it names" -- a channel shared by several routes must
/// never be drained twice, which would silently discard half its samples
/// from the second reader's point of view), and which measurement channels
/// found no reference to pair with at all.
struct RoutingPlan {
    std::vector<TransferRoute> routes;         ///< ascending measurement channel
    std::vector<int> distinctReferences;       ///< ascending; each read ONCE per hop
    std::vector<int> unroutedMeasurements;     ///< ascending; never paired with channel 0
};

/// Resolves every `Measurement` channel below `channelCount` to the
/// `Reference` channel sharing its transfer-function index. A transfer
/// function is a pair of channels agreeing on `transferFunction()`: exactly
/// one of them holds `ChannelRole::Reference`, the other
/// `ChannelRole::Measurement` -- this is what "each transfer function names
/// its own reference channel" (record §6) means in terms of the table B1
/// added, without a third field anywhere.
///
/// `channelCount` clamps like `ChannelConfig::snapshot()` does: negative
/// clamps to 0, and no channel at or beyond `rta::platform::kMaxChannels` is
/// ever examined, matching every other bounds rule in this table.
[[nodiscard]] inline RoutingPlan planRouting(const rta::platform::ChannelConfig& config,
                                              int channelCount) {
    using rta::platform::ChannelRole;
    using rta::platform::kMaxChannels;

    RoutingPlan plan;
    const int clamped = std::clamp(channelCount, 0, kMaxChannels);

    for (int m = 0; m < clamped; ++m) {
        if (config.role(m) != ChannelRole::Measurement) continue;

        const int tf = config.transferFunction(m);
        int referenceChannel = -1;
        for (int r = 0; r < clamped; ++r) {
            if (config.role(r) == ChannelRole::Reference && config.transferFunction(r) == tf) {
                referenceChannel = r;
                break;
            }
        }

        if (referenceChannel < 0) {
            plan.unroutedMeasurements.push_back(m);
            continue;
        }

        plan.routes.push_back(TransferRoute{tf, referenceChannel, m});
        // Every distinct reference channel appears exactly once, regardless
        // of how many measurement channels name it -- that is the whole
        // point of collecting this list separately from `routes`.
        if (std::find(plan.distinctReferences.begin(), plan.distinctReferences.end(),
                      referenceChannel) == plan.distinctReferences.end()) {
            plan.distinctReferences.push_back(referenceChannel);
        }
    }

    // `routes` is already ascending-by-measurement-channel from the loop
    // above; `distinctReferences` is not (a later measurement channel can
    // resolve to an earlier reference), so it is sorted once here.
    std::sort(plan.distinctReferences.begin(), plan.distinctReferences.end());
    return plan;
}

}  // namespace rta::measure
