// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// See docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.3, §3.4.
#pragma once

#include "measure/Snapshot.h"

#include <utility>

namespace rta::measure {

/// Anything a view can ask "what is the latest measurement" -- `Analyser`
/// itself (offline, this wave), and `AnalysisThread` (a later wave, reading
/// off its own audio-callback-fed bus) both satisfy this the same way, so a
/// view never needs to know which one it was handed.
struct SnapshotSource {
    virtual ~SnapshotSource() = default;

    [[nodiscard]] virtual SnapshotPtr latest() const = 0;
};

/// A fixed snapshot behind the `SnapshotSource` interface -- for tests, and
/// for the offscreen snapshot tool, which renders `RtaView` from one
/// `makeSyntheticSnapshot` result with no timer and no thread running.
class StaticSnapshotSource final : public SnapshotSource {
public:
    StaticSnapshotSource() = default;
    explicit StaticSnapshotSource(SnapshotPtr snapshot) : snapshot_(std::move(snapshot)) {}

    void set(SnapshotPtr snapshot) { snapshot_ = std::move(snapshot); }

    [[nodiscard]] SnapshotPtr latest() const override { return snapshot_; }

private:
    SnapshotPtr snapshot_;
};

}  // namespace rta::measure
