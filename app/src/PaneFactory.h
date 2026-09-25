// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.6 (Wave E / T10).
#pragma once

#include "measure/SnapshotSource.h"
#include "view/WorkspaceView.h"

/// `MainComponent`'s pane factory (`workspace_` is built with this) -- the
/// one place in the whole lane that names `RtaView`, `TransferView` AND
/// `SplView` alongside the `SnapshotSource` they all read from.
///
/// Split into its own translation unit, separate from MainComponent.cpp,
/// for TWO reasons: (1) it needs only `SnapshotSource` -- not
/// `AnalysisThread`, `ApiServer`, `DevicePanel` or any of MainComponent's
/// other heavy dependencies -- so a test can link it (plus RtaView.cpp/
/// TransferView.cpp/SplView.cpp, which `rtatool_view_tests` already links)
/// without pulling in the whole composition root; (2) exposing it here
/// lets a test call the REAL production closure and check what it builds
/// for each `rta::view::PaneView`, rather than a hand-copied local fixture
/// (the shape `app/tests_juce/test_workspace_view.cpp` uses on purpose, for
/// a DIFFERENT question: "does WorkspaceView call whatever factory it is
/// handed"). A local copy cannot catch a regression in THIS function's own
/// switch, which is what happened here: its `PaneView::Spl` branch was
/// missing entirely and every SPL pane silently built an `RtaView` instead
/// (fix round, PR #26).
[[nodiscard]] rta::view::WorkspaceView::PaneFactory makePaneFactory(
    rta::measure::SnapshotSource& source);
