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

class MainComponentAlign {
public:
    /// Called once by MainComponent::wireAlign (MainComponentAlign.cpp).
    void attach(rta::platform::OutputEngine& engine, rta::trace::TraceLibrary& library,
                rta::measure::AlignHost host) {
        runner_.attach(engine, library, std::move(host));
    }

    /// The wiring `makePaneFactory` gives every AlignPaneView it builds.
    [[nodiscard]] rta::view::AlignPaneBinding binding() noexcept {
        return rta::view::AlignPaneBinding{ &runner_ };
    }

    /// From MainComponent's 2 Hz timer.
    void poll() { runner_.poll(); }

    /// A solo step or the SUM capture is in flight. LOCATE, CAL, SWEEP and
    /// APPLY read this (through `captureBusy()`) and refuse while it is true.
    [[nodiscard]] bool busy() const noexcept { return runner_.busy(); }

    [[nodiscard]] rta::measure::AlignRunner& runnerForTest() noexcept { return runner_; }

private:
    rta::measure::AlignRunner runner_;
};
