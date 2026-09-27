// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rtatool_view_tests. station-3 STORE fix round 1, HIGH
// F1: `TransferView::effectiveSource(pane)` is the preference+availability
// fallback rule `renderTo()` already draws with (record §6), now exposed so
// MainComponentStore.cpp's STORE readout can name which panes are showing
// MTW without re-deriving the rule. This file proves the rule itself,
// independent of any MainComponent/AnalysisThread machinery -- a
// StaticSnapshotSource with a hand-built Snapshot is enough, no timer, no
// message loop, no real engine.
#include <catch2/catch_test_macros.hpp>

#include "measure/Snapshot.h"
#include "measure/SnapshotSource.h"
#include "view/TransferView.h"

#include <memory>

using rta::measure::MtwBlock;
using rta::measure::Snapshot;
using rta::measure::SnapshotPtr;
using rta::measure::StaticSnapshotSource;
using rta::measure::TransferBlock;
using rta::view::TransferPane;
using rta::view::TransferSource;
using rta::view::TransferView;

namespace {

// Presence is all `effectiveSource` reads off either block -- neither needs
// realistic data for this file's own purpose.
SnapshotPtr snapshotWith(bool hasFixed, bool hasMtw) {
    auto snap = std::make_shared<Snapshot>();
    snap->sequence = 1;
    snap->sampleRate = 48000.0;
    snap->fftSize = 2048;
    if (hasFixed) snap->transfer = TransferBlock{};
    if (hasMtw) snap->mtw = MtwBlock{};
    return snap;
}

}  // namespace

// CATCHES: HIGH F1's own defect -- a caller (the STORE readout) assuming
// every TRANSFER pane shows MTW instead of asking each pane what it is
// ACTUALLY showing.
TEST_CASE("effectiveSource is Mtw for every pane when both blocks are present and nothing was toggled",
         "[transfer-view][effective-source]") {
    const StaticSnapshotSource source(snapshotWith(true, true));
    TransferView view(source);

    CHECK(view.effectiveSource(TransferPane::Magnitude) == TransferSource::Mtw);
    CHECK(view.effectiveSource(TransferPane::Phase) == TransferSource::Mtw);
    CHECK(view.effectiveSource(TransferPane::Coherence) == TransferSource::Mtw);
}

TEST_CASE("effectiveSource honours an explicit FIXED toggle when the fixed block is present",
         "[transfer-view][effective-source]") {
    const StaticSnapshotSource source(snapshotWith(true, true));
    TransferView view(source);
    view.setSource(TransferPane::Magnitude, TransferSource::Fixed);
    view.setSource(TransferPane::Phase, TransferSource::Fixed);
    view.setSource(TransferPane::Coherence, TransferSource::Fixed);

    // Mixed case: every pane explicitly set to FIXED -- HIGH F1's own
    // "all panes on FIXED: no clause" scenario, at the rule level.
    CHECK(view.effectiveSource(TransferPane::Magnitude) == TransferSource::Fixed);
    CHECK(view.effectiveSource(TransferPane::Phase) == TransferSource::Fixed);
    CHECK(view.effectiveSource(TransferPane::Coherence) == TransferSource::Fixed);
}

TEST_CASE("effectiveSource on a mixed toggle names only the panes actually on MTW",
         "[transfer-view][effective-source]") {
    const StaticSnapshotSource source(snapshotWith(true, true));
    TransferView view(source);
    view.setSource(TransferPane::Phase, TransferSource::Fixed);
    // Magnitude and Coherence left at their Mtw default.

    CHECK(view.effectiveSource(TransferPane::Magnitude) == TransferSource::Mtw);
    CHECK(view.effectiveSource(TransferPane::Phase) == TransferSource::Fixed);
    CHECK(view.effectiveSource(TransferPane::Coherence) == TransferSource::Mtw);
}

// CATCHES: HIGH F1's second named trigger -- TransferView.cpp:143-147's own
// fallback to FIXED when `snapshot->mtw` is absent (mtwEnabled == false, or
// no reference fed yet) must be visible through `effectiveSource` too, not
// only through renderTo()'s pixels.
TEST_CASE("effectiveSource falls back to FIXED when mtw is absent, even though the preference is Mtw",
         "[transfer-view][effective-source]") {
    const StaticSnapshotSource source(snapshotWith(true, false));
    TransferView view(source);  // default preference is Mtw for every pane

    CHECK(view.effectiveSource(TransferPane::Magnitude) == TransferSource::Fixed);
    CHECK(view.effectiveSource(TransferPane::Phase) == TransferSource::Fixed);
    CHECK(view.effectiveSource(TransferPane::Coherence) == TransferSource::Fixed);
}

TEST_CASE("effectiveSource falls back to MTW when fixed is absent, even though the preference is Fixed",
         "[transfer-view][effective-source]") {
    const StaticSnapshotSource source(snapshotWith(false, true));
    TransferView view(source);
    view.setSource(TransferPane::Magnitude, TransferSource::Fixed);

    CHECK(view.effectiveSource(TransferPane::Magnitude) == TransferSource::Mtw);
}
