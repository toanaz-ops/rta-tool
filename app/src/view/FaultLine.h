// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: the decision is plain data so a
// test can drive it without a window.
#pragma once

#include "rta/platform/Fault.h"

#include <cstdint>
#include <string>

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
/// `sequence` is the SUM of the sources' sequences. Each only ever increments,
/// so the sum differs from the last one painted exactly when any source
/// recorded something new -- the same compare `DevicePanel` already did against
/// one source, extended without a second member.
///
/// A THIRD source, the capture bus's `resyncAckTimeouts()` (monotonic, never
/// reset): the consumer drained its rings after waiting 500 ms for the audio
/// callback to acknowledge the gate and got no answer, so the first block after
/// a device change may start misaligned between channels. Nothing read that
/// counter before this. It shows only when neither fault above is active -- a
/// dead device or a dead analysis thread explains it better -- and its count is
/// part of the sequence either way, so a new timeout repaints the slot.
[[nodiscard]] inline rta::platform::Fault selectFault(const rta::platform::Fault& audio,
                                                      const rta::platform::Fault& analysis,
                                                      std::uint64_t resyncAckTimeouts = 0) {
    rta::platform::Fault out;
    out.sequence = audio.sequence + analysis.sequence + static_cast<std::uint32_t>(resyncAckTimeouts);
    if (audio.kind != rta::platform::Fault::Kind::None) {
        out.kind = audio.kind;
        out.message = audio.message;
    } else if (analysis.kind != rta::platform::Fault::Kind::None) {
        out.kind = analysis.kind;
        out.message = (analysis.kind == rta::platform::Fault::Kind::Recovering ? "ANALYSIS RECOVERING: "
                                                                              : "ANALYSIS STOPPED: ") +
                      analysis.message;
    } else if (resyncAckTimeouts > 0) {
        out.kind = rta::platform::Fault::Kind::DeviceError;
        out.message = "CAPTURE RESYNC UNACKNOWLEDGED x" + std::to_string(resyncAckTimeouts) +
                      ": channels may start misaligned after the last device change";
    }
    return out;
}

}  // namespace rta::view
