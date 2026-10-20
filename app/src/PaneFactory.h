// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.6 (Wave E / T10).
#pragma once

#include "measure/SnapshotSource.h"
#include "view/AlignPaneView.h"
#include "view/EqPaneView.h"
#include "view/Rt60PaneView.h"
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
///
/// `eq` (L7-EQ UI lane, plan T4) is the EQ pane's wiring: the model it draws
/// and the export actions it calls. Both are owned by `MainComponentEq`, which
/// outlives every pane built here, so the committed filter list survives the
/// pane being rebuilt on a selector click. Required, never defaulted: a
/// factory with no model would have to build SOMETHING for `PaneView::Eq`, and
/// building the wrong pane silently is exactly the defect described above.
///
/// `align` (ALIGN-UI lane, plan T4) is the ALIGN pane's wiring, with the same
/// ownership and the same reason as `eq`: the runner and the answers live in
/// MainComponentAlign, so a pane rebuilt by a selector click keeps the
/// operator's answers and the last verdict.
///
/// `rt60` (RT60-UI lane, plan T3) is the RT60 pane's wiring, owned by
/// MainComponentRt60 for the same reason: the cached analysis, the selection
/// and the output/TAIL choices survive the pane being rebuilt.
[[nodiscard]] rta::view::WorkspaceView::PaneFactory makePaneFactory(
    rta::measure::SnapshotSource& source, rta::view::EqPaneBinding eq,
    rta::view::AlignPaneBinding align, rta::view::Rt60PaneBinding rt60);
