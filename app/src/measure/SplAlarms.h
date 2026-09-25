// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// enforced by the measure_has_no_framework_deps ctest.
// Lane L6a task W2-B (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md §6, §13 Q5).
#pragma once

#include "measure/Snapshot.h"
#include "measure/SplConfig.h"
#include "measure/SplHistory.h"

#include "rta/meter/Alarm.h"
#include "rta/meter/Block.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rta::measure {

/// §13 Q5: over the SAME blocks, the sliding maximum windowed Leq is >= the
/// consecutive-fixed (tumbling, grid-aligned-at-0) maximum, because every
/// tumbling window is also one of the sliding positions -- equal only when
/// the signal is constant. `combineBlocks` itself takes no window POSITION,
/// only a tail already selected by its caller (`AnalysisPublish.cpp`'s own
/// pattern), so these two own the windowing these acceptance rows compare.
[[nodiscard]] std::optional<double> slidingMaxLeqDb(std::span<const rta::meter::Block> blocks,
                                                    double sampleRate, double referenceOffsetDb,
                                                    std::uint64_t windowBlocks) noexcept;
[[nodiscard]] std::optional<double> consecutiveFixedMaxLeqDb(std::span<const rta::meter::Block> blocks,
                                                             double sampleRate,
                                                             double referenceOffsetDb,
                                                             std::uint64_t windowBlocks) noexcept;

/// A proxy window's own settings -- DATA, with NO default margin (record §6,
/// §13 Q5; memory/a-threshold-read-off-a-grid-is-that-grids-floor.md).
///
/// Two precedents exist and neither is shipped as a default:
///   UK Noise Council "Pop Code" cl. 4.12: a 15-minute proxy alarm is
///   "typically set some 2-3 dB(A) above the 15 minute value" -- an
///   OPERATOR HABIT stated in the code of practice, not a formula.
///   Belgium VLAREM II art. 5.32.2.2bis sec.2 1 deg: `LAeq,15min <= 102`
///   dB(A) deems `LAeq,60min <= 100` dB(A) satisfied -- a regulator's own
///   substitution, at ONE stated pair of numbers, not a general rule.
/// Reusing either as a shipped amber margin would be this project
/// ORIGINATING a number nobody published; the operator types one instead.
struct SplProxyWindow {
    std::uint64_t targetWindowBlocks = 0;  ///< the window this proxy stands in for
    std::uint64_t proxyWindowBlocks = 0;   ///< the proxy's own, shorter window
    double marginDb = 0.0;                 ///< the OPERATOR's margin; 0.0 = none applied
};

/// One configured alarm, over the SLIDING windowed Leq -- the conservative
/// of the two policies above, and therefore the one this class runs (record
/// §13 Q5 default). No hysteresis and no debounce: `rta::meter::AlarmLatch`
/// already carries that decision (record §6); this class only feeds it.
class SplAlarm {
public:
    explicit SplAlarm(SplAlarmSpec spec) : spec_(std::move(spec)) {}

    /// Recomputes the sliding window ending at the newest block in `blocks`
    /// (the tail of `spec_.windowBlocks`, or the whole span if shorter) and
    /// updates the latch. Appends exactly one marker to `history` on a
    /// `Fired` or `Cleared` transition -- never on `None` -- whose `value` is
    /// the windowed dB that caused it, BITWISE equal to what the latch
    /// compared (B3).
    void update(std::span<const rta::meter::Block> blocks, double sampleRate, double blockSeconds,
               double referenceOffsetDb, std::uint64_t currentBlockIndex, SplHistory& history);

    /// Returns `Snapshot.h`'s own `SplAlarmReading` -- PR #26 fix round item
    /// 6: "there must be one type for one fact". This used to return a
    /// separate `SplAlarmReport` carrying the same state/limitDb/headroomDb
    /// plus `windowBlocks`/`sinceBlock`; those two fields now live on
    /// `SplAlarmReading` itself, so there is one alarm-reading type, not two.
    /// `valueDb` carries the most recent windowed dB this alarm computed
    /// (float, per that type's own convention), even while the window is
    /// still filling.
    [[nodiscard]] SplAlarmReading report() const;
    [[nodiscard]] const SplAlarmSpec& spec() const noexcept { return spec_; }

private:
    SplAlarmSpec spec_;
    rta::meter::AlarmLatch latch_;
    SplAlarmState state_ = SplAlarmState::Clear;
    std::optional<std::uint64_t> sinceBlock_;
    std::optional<double> headroomDb_;
    float valueDb_ = static_cast<float>(kLevelFloorDb);
};

/// Every configured alarm for one session, updated together each block.
class SplAlarms {
public:
    explicit SplAlarms(std::vector<SplAlarmSpec> specs);

    void update(std::span<const rta::meter::Block> blocks, double sampleRate, double blockSeconds,
               double referenceOffsetDb, std::uint64_t currentBlockIndex, SplHistory& history);

    [[nodiscard]] std::vector<SplAlarmReading> reports() const;

private:
    std::vector<SplAlarm> alarms_;
};

}  // namespace rta::measure
