// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests_juce. F4 (docs/HUMAN-QA-QUEUE.md D11, PR #43
// F4): Session Save used to write only `{currentPaneSpec_()}` -- exactly the
// pane `currentPaneView_` names -- which collapsed a multi-pane workspace to
// one pane on every re-save, even though `restoreWorkspaceFromSession` (Open)
// already accepted 1-3 panes. This file proves the round trip against a REAL
// MainComponent, same shape as test_main_component_session.cpp beside it:
// only a real `MainComponentSession` wired to the real `WorkspaceView` can
// prove Save actually reads every pane back, not just that the pure
// `WorkspaceView::paneSpecs()` function returns the right thing in isolation
// (that half is test_workspace_view.cpp's job).
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "trace/Workspace.h"

#include <juce_core/juce_core.h>

#include <filesystem>
#include <string>
#include <vector>

using rta::trace::PaneSpec;

namespace {

/// Same shape every other TempDir fixture in this tree already uses.
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char* name)
        : path(std::filesystem::temp_directory_path() / "rta-test-mc-session-multipane" / name) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

}  // namespace

TEST_CASE("Save writes every pane of a multi-pane workspace, then Open rebuilds all of them",
         "[main_component_session_multipane]") {
    TempDir dir("three-pane");

    MainComponent writer;
    writer.setSyntheticMode(true);
    // Three DISTINCT pane kinds (never three copies of the same view) so a
    // mutant that collapses to "the first pane" or "the last pane" is
    // unambiguous either way -- and distinct, non-uniform weights so a
    // mutant that keeps the pane COUNT but drops the WEIGHTS is still
    // visible.
    const std::vector<PaneSpec> panes{PaneSpec{"rta", 2.0f}, PaneSpec{"transfer", 1.0f},
                                      PaneSpec{"spl", 1.0f}};
    MainComponentTestAccess::restoreWorkspaceForTest(writer, panes);
    REQUIRE(MainComponentTestAccess::paneSpecsForTest(writer).size() == 3u);

    MainComponentTestAccess::saveSessionForTest(writer, juce::File(dir.path.string()));
    CHECK(MainComponentTestAccess::readoutForTest(writer).startsWith("SAVED"));

    MainComponent reader;
    reader.setSyntheticMode(true);
    MainComponentTestAccess::openSessionForTest(reader, juce::File(dir.path.string()));
    CHECK(MainComponentTestAccess::readoutForTest(reader).startsWith("OPENED"));

    // THE mutant this proves against (F4's own, docs/HUMAN-QA-QUEUE.md D11):
    // `performSave` reverting to `doc.panes = {currentPaneSpecs_().front()}`
    // (or any other "keep only one pane" shape) -- the SAVED FILE would then
    // name exactly one pane, and Open would rebuild exactly one, regardless
    // of what the writer's own live workspace_ still holds in memory.
    const auto reopened = MainComponentTestAccess::paneSpecsForTest(reader);
    REQUIRE(reopened.size() == 3u);
    CHECK(reopened[0].view == "rta");
    CHECK(reopened[1].view == "transfer");
    CHECK(reopened[2].view == "spl");
    // normalisePanes scales {2,1,1} to sum to 1 among themselves: 0.5/0.25/0.25.
    CHECK(reopened[0].weight == 0.5f);
    CHECK(reopened[1].weight == 0.25f);
    CHECK(reopened[2].weight == 0.25f);
}

TEST_CASE("Save on the default single-pane workspace still writes exactly one pane",
         "[main_component_session_multipane]") {
    // The common case, unchanged by F4's fix: a brand-new MainComponent
    // (one default `rta` pane) round-trips to exactly one pane, not three
    // and not zero -- guards against an overcorrection that always writes
    // `kMaxPanes` regardless of what the workspace actually holds.
    TempDir dir("single-pane");

    MainComponent writer;
    writer.setSyntheticMode(true);
    REQUIRE(MainComponentTestAccess::paneSpecsForTest(writer).size() == 1u);

    MainComponentTestAccess::saveSessionForTest(writer, juce::File(dir.path.string()));
    CHECK(MainComponentTestAccess::readoutForTest(writer).startsWith("SAVED"));

    MainComponent reader;
    reader.setSyntheticMode(true);
    MainComponentTestAccess::openSessionForTest(reader, juce::File(dir.path.string()));

    const auto reopened = MainComponentTestAccess::paneSpecsForTest(reader);
    REQUIRE(reopened.size() == 1u);
    CHECK(reopened[0].view == "rta");
}
