// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src.
//
// ALIGN-UI lane, task T6 (docs/plans/2026-10-20-align-ui-lane-plan.md,
// decision 1). MainComponent's ALIGN half, in the shape MainComponentEq and
// MainComponentSweep already give theirs: a small class MainComponent owns as
// a member, so MainComponent.h gains one member and one accessor, not a state
// machine. It owns the AlignRunner -- NOT the pane, which MainComponentPanes.cpp
// rebuilds on every selector click -- so the operator's answers and the last
// verdict survive switching to TRANSFER and back.
//
// What the runner needs from the real app (the device, the live snapshot, the
// output count, the busy flags) is wired in MainComponentAlign.cpp.
//
// Mutual exclusion is two-way. ALIGN refuses while LOCATE, CAL, SWEEP or VERIFY
// hold the output or the shared capture (the runner's `captureBusy` reading),
// and each of those refuses while `busy()` here is true (`captureBusy()` in
// MainComponent.h) -- LOCATE ignores `setSource`'s return value and disarms at
// the end of its capture, so an overlap would silently kill the excitation
// ALIGN is measuring. APPLY also refuses while ALIGN runs (MainComponentDelay.cpp).
#pragma once

#include "measure/AlignRunner.h"
#include "view/AlignPaneView.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

class MainComponentAlign {
public:
    /// Called once by MainComponent::wireAlign (MainComponentAlign.cpp).
    void attach(rta::platform::OutputEngine& engine, rta::trace::TraceLibrary& library,
                rta::measure::AlignHost host) {
        library_ = &library;
        runner_.attach(engine, library, std::move(host));
    }

    /// What MainComponent's SWEEP half offers ALIGN (wave B, T10/T11): start a
    /// sweep on a named 0-based output, find a stored IR by trace id, read the
    /// sweep's own readout, and whether it is running. Set once by wireAlign.
    struct SweepAccess {
        std::function<bool(int outputChannel)> start;
        std::function<std::shared_ptr<const rta::measure::SweepIr>(const std::string&)> find;
        std::function<std::string()> readout;
        std::function<bool()> busy;
    };
    void attachSweep(SweepAccess sweep) { sweep_ = std::move(sweep); }

    /// The wiring `makePaneFactory` gives every AlignPaneView it builds.
    [[nodiscard]] rta::view::AlignPaneBinding binding() noexcept {
        return rta::view::AlignPaneBinding{ &runner_, irHooks() };
    }

    /// SWEEP MAIN / SWEEP SUB: sweep on the output named for that source and
    /// remember which source the result belongs to. The same busy predicates
    /// as every other capture: MainComponentSweep refuses while ALIGN runs.
    void sweepOnSide(rta::measure::Source side);
    /// MainComponentSweep's `Host::swept`: auto-binds the just-swept IR to the
    /// source whose button asked for it.
    void onSwept(const std::string& traceId, int outputChannel);

    /// From MainComponent's 2 Hz timer.
    void poll() { runner_.poll(); }

    /// A solo step or the SUM capture is in flight. LOCATE, CAL, SWEEP and
    /// APPLY read this (through `captureBusy()`) and refuse while it is true.
    [[nodiscard]] bool busy() const noexcept { return runner_.busy(); }

    [[nodiscard]] rta::measure::AlignRunner& runnerForTest() noexcept { return runner_; }

private:
    [[nodiscard]] rta::view::AlignIrHooks irHooks();

    rta::measure::AlignRunner runner_;
    rta::trace::TraceLibrary* library_ = nullptr;
    SweepAccess sweep_;
    /// Which source's button started the sweep now running, and on which output.
    struct PendingSweep {
        rta::measure::Source side = rta::measure::Source::Main;
        int outputChannel = 0;
    };
    std::optional<PendingSweep> pending_;
};
