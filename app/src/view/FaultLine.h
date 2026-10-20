// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: the decision is plain data so a
// test can drive it without a window.
#pragma once

#include "rta/platform/Fault.h"

namespace rta::view {

/// What the device panel's single fault slot shows when TWO sources can fault:
/// the audio device (`AudioIo::lastFault`) and the analysis thread
/// (`AnalysisThread::lastFault`).
///
/// An exception on the analysis thread ends that thread for good -- `latest()`
/// keeps pointing at the last snapshot, so the display freezes on a plausible
/// curve (audit prj010-fable-audit-20261021, medium). The operator can only
/// know if the slot says so.
///
/// Priority: a device fault wins, because it is the more actionable one and a
/// dead device usually explains a quiet analysis thread; the analysis fault
/// shows when the device is healthy. The analysis fault is prefixed so the
/// operator can tell the two apart.
///
/// `sequence` is the SUM of both sources' sequences. Each only ever
/// increments, so the sum differs from the last one painted exactly when either
/// source recorded something new -- the same compare `DevicePanel` already did
/// against one source, extended to two without a second member.
[[nodiscard]] inline rta::platform::Fault selectFault(const rta::platform::Fault& audio,
                                                      const rta::platform::Fault& analysis) {
    rta::platform::Fault out;
    out.sequence = audio.sequence + analysis.sequence;
    if (audio.kind != rta::platform::Fault::Kind::None) {
        out.kind = audio.kind;
        out.message = audio.message;
    } else if (analysis.kind != rta::platform::Fault::Kind::None) {
        out.kind = analysis.kind;
        out.message = "ANALYSIS STOPPED: " + analysis.message;
    }
    return out;
}

}  // namespace rta::view
