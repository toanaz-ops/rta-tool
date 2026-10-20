// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src.
//
// RT60-UI lane, task T5 (docs/plans/2026-10-20-rt60-ui-lane-plan.md, decisions
// 1, 2, 9, 11). MainComponent's RT60 half, in the shape MainComponentAlign and
// MainComponentEq already give theirs: a small class MainComponent owns as a
// member, so MainComponent.h gains one member and one accessor, not a state
// machine. It owns what must survive the pane being rebuilt by a selector click
// -- the selection, the cached analysis, the output and TAIL choices -- and
// NOT the pane, which MainComponentPanes.cpp rebuilds.
//
// What it needs from the real app (the library, the SWEEP half's IR store and
// readout, the output count) arrives as callbacks wired in MainComponentRt60.cpp,
// the way ALIGN's `SweepAccess` does it: this class never includes MainComponent
// or MainComponentSweep.
//
// The analysis runs on the message thread, once per selection change, and is
// cached here (decision 11: one IR, seven bands, about 150k samples; the pane
// prints the measured wall time). `Rt60Analysis.h` carries no `rta/ir/*`, so
// nothing in this file can reach the deconvolver either.
//
// Mutual exclusion is the SWEEP half's, unchanged: the pane's SWEEP goes through
// `MainComponentSweep::clickedOnOutput`, which `captureBusy()` already counts, so
// LOCATE / CAL / ALIGN / VERIFY refuse while it runs and it refuses while they do.
#pragma once

#include "measure/Rt60Analysis.h"
#include "trace/TraceLibrary.h"
#include "view/Rt60PaneView.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class MainComponentRt60 {
public:
    /// What MainComponent's SWEEP half offers this one (set once by wireRt60).
    struct SweepAccess {
        /// Sweep on a named 0-based output with a per-press tail, tagged as RT60's.
        std::function<bool(int outputChannel, double tailSeconds)> start;
        std::function<std::shared_ptr<const rta::measure::SweepIr>(const std::string& traceId)> find;
        std::function<std::string()> readout;
        std::function<bool()> busy;
        std::function<int()> outputChannels;
    };

    void attach(rta::trace::TraceLibrary& library, SweepAccess sweep) {
        library_ = &library;
        sweep_ = std::move(sweep);
    }

    /// The wiring `makePaneFactory` gives every Rt60PaneView it builds.
    [[nodiscard]] rta::view::Rt60PaneBinding binding();

    /// Picks a trace and analyses it. A trace whose IR is not in memory is
    /// selected too, and its result is the `NoIr` refusal (decision 9).
    void select(const std::string& traceId);

    /// MainComponentSweep's `Host::swept`, for a press this pane requested: the
    /// new trace becomes the selection. The requester tag guarantees no other
    /// pane's sweep reaches here, so nothing but RT60's own press auto-selects.
    void onSwept(const std::string& traceId) { select(traceId); }

    // --- test hooks, each referenced by spelling from app/tests_juce ---------
    [[nodiscard]] rta::view::Rt60Settings& settingsForTest() noexcept { return settings_; }
    [[nodiscard]] const std::string& selectedIdForTest() const noexcept { return selectedId_; }
    [[nodiscard]] std::shared_ptr<const rta::measure::Rt60Result> resultForTest() const noexcept { return result_; }
    /// How many times `select` ran an analysis, so a test can prove "once per
    /// selection change" and not "once per tick".
    [[nodiscard]] int analysisCountForTest() const noexcept { return analysisCount_; }

private:
    [[nodiscard]] std::vector<rta::view::Rt60Choice> choices() const;
    [[nodiscard]] bool selectedIrInMemory() const;

    rta::trace::TraceLibrary* library_ = nullptr;
    SweepAccess sweep_;
    rta::view::Rt60Settings settings_;
    std::string selectedId_;
    /// The trace's name when it was picked, so the header survives the trace
    /// leaving the library.
    std::string selectedName_;
    std::shared_ptr<const rta::measure::Rt60Result> result_;
    std::uint64_t revision_ = 0;
    int analysisCount_ = 0;
};
