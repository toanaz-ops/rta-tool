// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Record §6, task B3: the spatial average is the published trace, plus one
// soloed position; every other member publishes a summary and nothing else.
#pragma once

#include "measure/Snapshot.h"

#include "rta/dsp/SpatialAverage.h"
#include "rta/dsp/TransferEstimator.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rta::measure {

/// Why a member is refused, named rather than a bare `bool` -- the same
/// "distinct, named reason" rule the capture sequencer (B5) will apply to
/// captures. `None` is not a refusal; it is the accepted case's own value in
/// the enum, so a caller can `if (reason != MemberRefusal::None)` without a
/// second type.
enum class MemberRefusal { None, DifferentReference };

/// One group member: which route it is (`tfIndex`, `AnalysisThread`'s
/// per-route ordinal -- see that class's own comment on why `tfIndex` is a
/// grouping tag, not an array index), which reference channel its route
/// names (what `addMember` checks two members against each other on), its
/// display name, and its trim `u_i` (record §3/§7: a linear weight,
/// default 1, entered in the UI as a dB trim).
struct AverageGroupMember {
    int tfIndex = -1;
    int referenceChannel = -1;
    std::string name;
    double trim = 1.0;
};

/// What one `publish()` call produces: the group's own average (absent
/// under the same condition `rta::dsp::spatialAverage` returns
/// `std::nullopt`), one summary per member always, and the full curve of at
/// most one SOLOED member.
struct AverageGroupPublish {
    std::optional<AverageBlock> average;
    std::vector<PositionSummary> positions;
    std::optional<TransferBlock> soloTransfer;
};

/// A measurement group (record §0's SMPTE ST 202 §5.2 motivation): several
/// transfer functions against ONE shared reference, combined into a spatial
/// average. Owns the member list, each member's trim, the `SpatialMode`,
/// and which member (if any) is currently soloed -- everything §6 says a
/// group needs beyond the per-hop `TransferSnapshot`s themselves, which this
/// class never stores (see `publish`'s own comment on why).
class AverageGroup {
public:
    explicit AverageGroup(rta::dsp::SpatialMode mode = rta::dsp::SpatialMode::Db) noexcept
        : mode_(mode) {}

    /// Refuses (returns `DifferentReference`, the group left UNCHANGED) a
    /// member whose `referenceChannel` disagrees with the group's own --
    /// established by the FIRST accepted member, and never revisited after
    /// (record §6: "two systems measured against two references are two
    /// groups, not one average"). The empty group accepts anything.
    [[nodiscard]] MemberRefusal addMember(int tfIndex, int referenceChannel, std::string name,
                                          double trim = 1.0);

    [[nodiscard]] std::size_t memberCount() const noexcept { return members_.size(); }
    [[nodiscard]] const std::vector<AverageGroupMember>& members() const noexcept {
        return members_;
    }

    /// -1 (the default) means no member is soloed. Setting a `tfIndex` no
    /// current member holds is accepted (nothing to solo yet is not an
    /// error) and simply produces no `soloTransfer` until a matching member
    /// is added.
    void setSolo(int tfIndex) noexcept { solo_ = tfIndex; }
    void clearSolo() noexcept { solo_ = -1; }
    [[nodiscard]] int solo() const noexcept { return solo_; }

    /// `positions[i]` must be the `TransferSnapshot` for `members()[i]` --
    /// same index, same order. Deliberately NOT `std::optional` per member:
    /// a caller with a member whose engine has not engaged yet does not
    /// call this until every member has (`Analyser::publish`'s own
    /// `dualEngaged_ && frameCount() > 0` gate is the same shape one layer
    /// down). A size mismatch returns a summary-only result (one
    /// default-constructed `PositionSummary` per member, no average) rather
    /// than throwing -- this runs on the analysis thread every publish, and
    /// a transient mismatch during a routing change is not the same kind of
    /// error `spatialAverage`'s own grid check exists to catch.
    ///
    /// Allocates NOTHING that scales with `positions.size()` beyond
    /// `result.positions` itself (task B3(d), record §6's O(1)-in-N claim):
    /// `positions` and this group's own `trim`s are read through spans
    /// straight into `rta::dsp::spatialAverage`, never copied into a local
    /// buffer sized by N -- see that function's own body for why its
    /// allocation is O(bins), not O(N * bins).
    [[nodiscard]] AverageGroupPublish publish(
        std::span<const rta::dsp::TransferSnapshot> positions) const;

private:
    rta::dsp::SpatialMode mode_;
    std::vector<AverageGroupMember> members_;
    int solo_ = -1;
};

}  // namespace rta::measure
