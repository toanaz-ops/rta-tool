// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. Round of LOW findings on PRs #36/#37, item 2:
// `MainComponent`'s test-only trio (the real pane a click built, the
// analysis thread, and the trace library) used to be public member
// functions -- reachable from any caller, not only the two that actually
// need them. This struct is the one seam: `MainComponent` grants it (and
// only it) friendship, so the three stay private on the class itself and
// every other caller sees the same public surface a shipped build exposes.
//
// Two callers, both privileged for a stated reason, not by accident:
//   - app/tests_juce/test_main_component_panes.cpp -- proves the wiring
//     between `selectPaneView` and the real `makePaneFactory`/
//     `analysisThread_` that no lower-level test can see (that file's own
//     header comment).
//   - tools/snapshot.cpp -- drives `analysisThread_` directly to start
//     metering-only SPL logging before rendering main-live-spl.png, the
//     same "the real seam a user's click uses" reasoning `setSyntheticMode`
//     and `selectPaneView` already document there. Not a test binary, but
//     not shipped in `rtatool` either -- a dev/render tool, same footing as
//     the pane test.
#pragma once

#include "MainComponent.h"

struct MainComponentTestAccess {
    [[nodiscard]] static const juce::Component& pane(const MainComponent& c) {
        return c.paneComponentForTest();
    }
    [[nodiscard]] static rta::measure::AnalysisThread& analysisThread(MainComponent& c) {
        return c.analysisThreadForTest();
    }
    [[nodiscard]] static const rta::trace::TraceLibrary& library(const MainComponent& c) {
        return c.libraryForTest();
    }
};
