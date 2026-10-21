// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Decision record: docs/dsp/2026-10-21-analyser-rebuild.md.
#pragma once

#include "measure/Analyser.h"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace rta::measure {

/// The state machine behind "rebuild every Analyser, one per tick".
///
/// A device change, an Apply of a new reference delay and a SYNTHETIC toggle
/// each invalidate every Analyser. Building all `kMaxTransferFunctions` of them
/// in one call is ~107 ms of allocate-and-zero-fill on the analysis thread at
/// MTW defaults (measured, record sec.2): the drain, the SPL feed and any
/// `threadShouldExit()` check all wait for it. Building ONE per tick bounds the
/// stall to one Analyser (~15 ms) however many there are.
///
/// The price is a window in which some slots are rebuilt and some are not, and
/// the one rule that makes that window safe: a STALE slot (index >= `next_`)
/// holds an Analyser built for the previous configuration and must never be fed
/// or published. `isFresh()` is that question, asked by the drain before every
/// push and by the publish gate before every snapshot. `begin()` makes every
/// slot stale in the same call that observes the epoch/Apply, i.e. before the
/// drain of that tick, so no post-epoch sample can reach an old Analyser.
///
/// Slots rebuild in ascending order. A route's slot is its position in the
/// routing plan (dense from 0), so live routes always rebuild before the idle
/// slots behind them.
///
/// Analysis-thread only; no member is atomic.
class AnalyserRebuild {
public:
    /// Starts fully fresh: the constructor of the owning thread builds the
    /// whole set once, from the same configuration.
    explicit AnalyserRebuild(std::size_t slots) noexcept : slots_(slots), next_(slots) {}

    /// Every slot becomes stale and the sweep restarts at slot 0 with `config`.
    /// A second `begin()` mid-sweep simply restarts it: slots already rebuilt
    /// for the superseded configuration are stale again.
    void begin(const Analyser::Config& config) {
        config_ = config;
        next_ = 0;
    }

    [[nodiscard]] bool pending() const noexcept { return next_ < slots_; }
    [[nodiscard]] bool isFresh(std::size_t slot) const noexcept { return slot < next_; }
    [[nodiscard]] std::size_t freshCount() const noexcept { return next_; }
    [[nodiscard]] const Analyser::Config& config() const noexcept { return config_; }

    /// Rebuilds the next stale slot with `make(config)`. If `make` throws
    /// (std::bad_alloc is the real case) nothing changes: the slot stays stale,
    /// the old Analyser stays in place unfed, and the caller may retry.
    /// The replacement is built BEFORE the old one is released, so a failed
    /// build never leaves a null slot behind.
    template <class Make>
    void step(std::vector<std::unique_ptr<Analyser>>& analysers, Make&& make) {
        if (!pending()) return;
        std::unique_ptr<Analyser> fresh = std::forward<Make>(make)(config_);
        analysers[next_] = std::move(fresh);
        ++next_;
    }

private:
    std::size_t slots_;
    std::size_t next_;
    Analyser::Config config_;
};

}  // namespace rta::measure
