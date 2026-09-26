// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Task W2-E2b fix round (verifier HIGH finding): a ROUTE POSITION is not a
// CHANNEL NUMBER. Header-only and a pure function over `RoutingPlan` (the
// `RoutingPlan.h`/`SplLoggingDecision.h` precedent), so the resolution is
// provable with RTA_BUILD_APP=OFF.
#pragma once

#include "measure/RoutingPlan.h"

#include <cstddef>
#include <optional>

namespace rta::measure {

/// `AnalysisThread::armLocateCapture(routeIndex, ...)` -- and therefore the
/// calibration flow, which shares that same accumulator with Locate -- is
/// keyed by ROUTE POSITION: ascending-measurement-channel position within
/// `plan.routes`, exactly as `AnalysisThread::routeHopCount`'s own doc
/// comment states. Every OTHER `AnalysisThread` API the calibration flow
/// calls once a capture completes (`splBlockCount`, `setCalibrationInvalid`)
/// is keyed by CHANNEL NUMBER instead. Conflating the two silently uses
/// route position 0 as if it were channel 0: correct only when channel 0
/// happens to be the Measurement role's channel, and wrong the moment an
/// operator assigns Measurement to any other channel (one click away in
/// `ChannelRoleTable`) -- the block-index range comes from the WRONG
/// channel's counter, the record's own `channel` field mismatches, the
/// report payload builder's `calibrationRecord->channel == channel` check
/// then never matches on ANY channel, and `SplView` never shows the invalid
/// flag it should.
///
/// This is the ONE place that resolves route position to channel number.
/// `std::nullopt` when `routeIndex` is out of range for `plan.routes` --
/// including the "calibrator-only rig, no REF channel" case (record §8's own
/// fix-round finding: that rig's `plan.routes` is empty, because
/// `planRouting` only admits a Measurement channel that resolves to a
/// Reference one), which a caller already handles as "nothing to resolve"
/// the same way every other `AnalysisThread` channel API treats a negative
/// channel: a safe no-op, never a crash.
[[nodiscard]] inline std::optional<int> calibrationMeasurementChannel(const RoutingPlan& plan,
                                                                      int routeIndex) noexcept {
    if (routeIndex < 0 || static_cast<std::size_t>(routeIndex) >= plan.routes.size()) {
        return std::nullopt;
    }
    return plan.routes[static_cast<std::size_t>(routeIndex)].measurementChannel;
}

/// Fix round 3 (verifier MEDIUM, upgraded from LOW): `calibrationMeasurementChannel`
/// is resolved TWICE per calibration -- once when the START check completes,
/// once when the END check does -- and nothing stopped an operator from
/// reassigning Measurement/Reference roles in `ChannelRoleTable` in between.
/// Comparing a START check on channel 1 against an END check on channel 0
/// is not a drift measurement of anything: the two numbers describe
/// different signal paths. `Write` only when both checks resolved to the
/// SAME non-negative channel; `-1` (this header's own "no channel" sentinel,
/// matching every `AnalysisThread` channel API) on either side is
/// `RefuseNoMeasurementChannel`, never silently treated as "channel -1".
enum class CalibrationChannelDecision { Write, RefuseChannelMismatch, RefuseNoMeasurementChannel };

[[nodiscard]] constexpr CalibrationChannelDecision decideCalibrationRecordChannel(
    int startChannel, int endChannel) noexcept {
    if (startChannel < 0 || endChannel < 0) {
        return CalibrationChannelDecision::RefuseNoMeasurementChannel;
    }
    if (startChannel != endChannel) {
        return CalibrationChannelDecision::RefuseChannelMismatch;
    }
    return CalibrationChannelDecision::Write;
}

}  // namespace rta::measure
