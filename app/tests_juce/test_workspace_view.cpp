// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rtatool_view_tests. Decision 6 of
// docs/dsp/2026-08-29-display-layer-l5c.md.
//
// This is the file the lane's point rests on. docs/HANDOFF.md recorded, for
// two sessions running, that "the stored-trace path is built but unreached
// -- no place attaches TraceLibrary to RtaView". Test 3 below is the
// assertion that would have caught that gap the moment it was introduced:
// it does not merely check that WorkspaceView::setLibrary was CALLED, it
// reads the library back off each concrete pane and checks it is the one
// that was set.
//
// No window, no desktop peer, no message loop -- the same headless
// software-renderer path tools/snapshot.cpp already proves runs offscreen.
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <az_ui/az_ui.h>

#include "measure/Snapshot.h"
#include "measure/SnapshotSource.h"
#include "measure/SyntheticSnapshot.h"
#include "trace/Trace.h"
#include "trace/TraceLibrary.h"
#include "trace/Workspace.h"
#include "view/PaneRegistry.h"
#include "view/RtaView.h"
#include "view/TransferView.h"
#include "view/WorkspaceView.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using rta::measure::SnapshotPtr;
using rta::measure::StaticSnapshotSource;
using rta::trace::CaptureMeta;
using rta::trace::PaneSpec;
using rta::trace::Trace;
using rta::trace::TraceLibrary;
using rta::view::PaneView;
using rta::view::RtaView;
using rta::view::TransferView;
using rta::view::WorkspaceView;

/// Every test below builds a workspace through the SAME shape of factory
/// `MainComponent` builds in production (MainComponent.cpp's own
/// `makePaneFactory`) -- a local copy here, not a shared one, for the same
/// reason test_transfer_view_helpers.h's fixtures are not shared production
/// code: a test fixture that reused the exact production closure would stop
/// testing "does WorkspaceView call the factory it was handed correctly"
/// and start testing "does MainComponent's factory work", which is a
/// different question with a different failure mode.
WorkspaceView::PaneFactory makeFactory(const rta::measure::SnapshotSource& source) {
    return [&source](PaneView view) -> std::unique_ptr<juce::Component> {
        if (view == PaneView::Transfer) return std::make_unique<TransferView>(source);
        return std::make_unique<RtaView>(source);
    };
}

/// A flat magnitude trace, same fixture test_stored_trace_layer.cpp uses --
/// content does not matter to any test here, only that `library.add`
/// succeeds and that a later `setVisible` is a REAL flip (TraceLibrary does
/// not advance its revision for a setter that changes nothing).
Trace flatTrace(std::string id, float levelDb) {
    CaptureMeta meta;
    meta.id = std::move(id);
    meta.sampleRate = 48000.0;
    meta.fftSize = 4096;

    auto trace = Trace::make(std::move(meta),
                             std::vector<float>(rta::trace::pointCountFor(4096), levelDb));
    REQUIRE(trace.has_value());
    return std::move(*trace);
}

std::string addFlatTrace(TraceLibrary& library, std::string id, float levelDb) {
    auto name = "capture " + id;
    return library.add(flatTrace(std::move(id), levelDb), std::move(name), "A");
}

/// Carries BOTH bands (empty here -- irrelevant to every test in this file)
/// and a real `transfer` block, so a `TransferView` pane exercises its
/// stored-layer draw calls rather than taking the early
/// `!hasTransfer -> drawNoReferenceState(); return;` exit (TransferView.cpp)
/// that would otherwise make test 4's `REQUIRE(magBefore > 0)` fail before
/// the interesting assertions are even reached -- which is the point: a
/// snapshot with no transfer would make that REQUIRE catch the mistake
/// immediately instead of leaving the real assertions vacuously true.
SnapshotPtr snapshotWithTransfer(std::uint64_t sequence) {
    auto snap = std::make_shared<rta::measure::Snapshot>();
    snap->sequence = sequence;
    snap->sampleRate = 48000.0;
    snap->fftSize = 4096;
    snap->transfer = rta::measure::makeSyntheticTransfer(snap->fftSize, snap->sampleRate, 18);
    return snap;
}

/// Renders `view` into a throwaway image at a fixed area, bypassing
/// `paint()`/the repaint gate entirely -- the same contract
/// test_transfer_view_helpers.h's `renderView` relies on. `RtaView` and
/// `TransferView` both expose `renderTo(Graphics&, Rectangle<int>) const`
/// for exactly this: driving the draw deterministically, with no message
/// loop and no timer ever having to fire.
template <typename View>
void renderOffscreen(View& view, int width, int height) {
    juce::Image image(juce::Image::ARGB, width, height, true);
    juce::Graphics g(image);
    view.renderTo(g, juce::Rectangle<int>(0, 0, width, height));
}

}  // namespace

// CATCHES: WorkspaceView::resized() calling the wrong split primitive
// (horizontal instead of vertical), ignoring the specs' weights, ignoring
// `az::ui::gap`, or assigning the split rectangles to children out of
// order. Compares against `az::ui::splitVertically` called with the SAME
// normalised weights WorkspaceView itself computes -- not a second,
// hand-derived copy of the split math (that math already has its own tests
// in ui/tests/test_layout.cpp), so this test is purely about whether
// WorkspaceView wires the primitive up correctly.
TEST_CASE("a two-pane workspace gives both children the full width and the weighted height",
          "[workspace-view]") {
    const StaticSnapshotSource source;
    std::vector<PaneSpec> panes{ PaneSpec{ "rta", 3.0f }, PaneSpec{ "transfer", 1.0f } };

    WorkspaceView workspace(panes, makeFactory(source));
    workspace.setSize(1100, 760);
    workspace.resized();

    std::vector<float> weights;
    for (const auto& p : rta::trace::normalisePanes(panes)) weights.push_back(p.weight);
    const auto expected =
        az::ui::splitVertically(juce::Rectangle<int>(0, 0, 1100, 760), weights, az::ui::gap);

    REQUIRE(workspace.getNumChildComponents() == 2);
    REQUIRE(expected.size() == 2u);
    CHECK(workspace.getChildComponent(0)->getBounds() == expected[0]);
    CHECK(workspace.getChildComponent(1)->getBounds() == expected[1]);

    // The vertical-only half of decision 6: every pane spans the full width.
    CHECK(workspace.getChildComponent(0)->getWidth() == 1100);
    CHECK(workspace.getChildComponent(1)->getWidth() == 1100);
}

// CATCHES: a WorkspaceView that DROPS a pane whose `view` string
// `resolvePaneView` did not recognise, instead of falling back to `rta` --
// task brief's own words: "a missing child would shift every child after
// it". `getNumChildComponents() == 2` fails first and loudest if that
// happens; the second CHECK confirms the survivor is a REAL pane, not a
// placeholder with nothing drawn.
TEST_CASE("an unknown view name yields a pane, not a hole", "[workspace-view]") {
    const StaticSnapshotSource source;
    std::vector<PaneSpec> panes{ PaneSpec{ "spectrograph", 1.0f }, PaneSpec{ "rta", 1.0f } };

    WorkspaceView workspace(panes, makeFactory(source));

    REQUIRE(workspace.getNumChildComponents() == 2);
    CHECK(dynamic_cast<RtaView*>(workspace.getChildComponent(0)) != nullptr);
    CHECK(dynamic_cast<RtaView*>(workspace.getChildComponent(1)) != nullptr);
}

// CATCHES the exact defect docs/HANDOFF.md recorded for two sessions
// running: a WorkspaceView::setLibrary that is a no-op, that only reaches
// the first child, or whose LibraryConsumer dynamic_cast is wrong for one
// pane type. Reads the library back off each concrete pane through
// `library()` -- production API added in this task for exactly this --
// rather than inferring "it must have worked" from setLibrary having been
// CALLED, which is the weaker claim that let the original gap survive a
// whole session unnoticed.
TEST_CASE("the library reaches every pane", "[workspace-view]") {
    TraceLibrary library;
    const StaticSnapshotSource source;
    std::vector<PaneSpec> panes{ PaneSpec{ "rta", 1.0f }, PaneSpec{ "transfer", 1.0f } };

    WorkspaceView workspace(panes, makeFactory(source));

    auto* rta = dynamic_cast<RtaView*>(workspace.getChildComponent(0));
    auto* transfer = dynamic_cast<TransferView*>(workspace.getChildComponent(1));
    REQUIRE(rta != nullptr);
    REQUIRE(transfer != nullptr);

    // Asserted BEFORE setLibrary too: a library() that always returned
    // non-null would make the "== &library" check below unfalsifiable.
    CHECK(rta->library() == nullptr);
    CHECK(transfer->library() == nullptr);

    workspace.setLibrary(&library);

    CHECK(rta->library() == &library);
    CHECK(transfer->library() == &library);
}

// CATCHES two opposite regressions at once, the same two-directional shape
// test_stored_trace_layer.cpp's own cache tests use: (1) a WorkspaceView or
// pane wiring that re-forwards/rebuilds on every frame regardless of what
// changed -- rebuildCount would move on the sequence-only render, which the
// record's §8 repaint-cost check exists to forbid; and (2) wiring that never
// reaches a library edit at all -- rebuildCount would stay flat even after a
// REAL revision bump, which would make "the library reaches every pane"
// true in name only. Multiplying RtaView's and TransferView's own
// already-tested cache claims (test_stored_trace_layer.cpp,
// test_transfer_view.cpp) by a WorkspaceView is the whole point: the O(1)
// property has to survive composition, not just hold for one view in
// isolation.
TEST_CASE("advancing the live sequence does not rebuild any cached layer",
          "[workspace-view]") {
    TraceLibrary library;
    const auto id = addFlatTrace(library, "t1", -45.0f);
    REQUIRE_FALSE(id.empty());

    StaticSnapshotSource source;
    source.set(snapshotWithTransfer(1));

    std::vector<PaneSpec> panes{ PaneSpec{ "rta", 1.0f }, PaneSpec{ "transfer", 1.0f } };
    WorkspaceView workspace(panes, makeFactory(source));
    workspace.setLibrary(&library);

    auto* rta = dynamic_cast<RtaView*>(workspace.getChildComponent(0));
    auto* transfer = dynamic_cast<TransferView*>(workspace.getChildComponent(1));
    REQUIRE(rta != nullptr);
    REQUIRE(transfer != nullptr);

    renderOffscreen(*rta, 1100, 760);
    renderOffscreen(*transfer, 1100, 760);

    const auto rtaBefore = rta->storedLayer().rebuildCount();
    const auto magBefore = transfer->magnitudeLayer().rebuildCount();
    const auto phaseBefore = transfer->phaseLayer().rebuildCount();
    REQUIRE(rtaBefore > 0);
    REQUIRE(magBefore > 0);
    REQUIRE(phaseBefore > 0);

    // Advance the live sequence only -- nothing about the library moved.
    source.set(snapshotWithTransfer(2));
    renderOffscreen(*rta, 1100, 760);
    renderOffscreen(*transfer, 1100, 760);

    CHECK(rta->storedLayer().rebuildCount() == rtaBefore);
    CHECK(transfer->magnitudeLayer().rebuildCount() == magBefore);
    CHECK(transfer->phaseLayer().rebuildCount() == phaseBefore);

    // Now a REAL library edit -- revision moves, and each pane's cache must
    // rebuild exactly once: not skip it (stale image) and not rebuild twice
    // (the O(N)-per-frame regression this whole architecture exists to
    // prevent).
    REQUIRE(library.setVisible(id, false));
    renderOffscreen(*rta, 1100, 760);
    renderOffscreen(*transfer, 1100, 760);

    CHECK(rta->storedLayer().rebuildCount() == rtaBefore + 1);
    CHECK(transfer->magnitudeLayer().rebuildCount() == magBefore + 1);
    CHECK(transfer->phaseLayer().rebuildCount() == phaseBefore + 1);
}
