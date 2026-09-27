// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests_juce. test_main_component_session.cpp's own
// 400-line-cap split (PR #43 fix round): the verifier's HIGH F1 (a
// std::filesystem::path built from juce::File::getFullPathName().
// toStdString() decodes with the process's ACTIVE CODE PAGE on MSVC, not
// UTF-8, corrupting a non-ASCII folder name on any box whose code page is
// not itself UTF-8 -- CI's windows-latest included), MEDIUM F2 (Open never
// reported an unrecognised saved pane name), and MEDIUM F3 (five claimed
// behaviours -- the partial-folder skip count, the wholesale refusal when
// every blob fails, the visible flag, the pane selector's toggle sync, and
// the SAVE/OPEN buttons' own wiring -- had no mutant that could go RED).
//
// See test_main_component_session.cpp's own header comment for the shared
// reasoning (async FileChooser never driven directly; TraceLibrary's
// message-thread-only threading argument). TempDir/makeMeta/findButtonByText
// below are a deliberate small duplicate of that file's own copies --
// each under 15 lines, the same "a fixture this small is cheaper to
// duplicate than to share" call test_main_component_panes.cpp's own TempDir
// already makes elsewhere in this tree.
#include <catch2/catch_test_macros.hpp>

#include "CodeLines.h"
#include "JuceFsPath.h"
#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "trace/Trace.h"
#include "view/CrossoverPaneView.h"

#include <juce_core/juce_core.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using rta::trace::CaptureMeta;
using rta::trace::Trace;
using rta::view::PaneSelectorButton;
using rta::view::PaneView;

namespace {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char* name)
        : path(std::filesystem::temp_directory_path() / "rta-test-mc-session-fixround" / name) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

CaptureMeta makeMeta(std::string id) {
    CaptureMeta m;
    m.id = std::move(id);
    m.sampleRate = 48000.0;
    m.fftSize = 8;  // pointCountFor(8) == 5, matching the 5-point vectors below
    return m;
}

/// Same shape test_main_component_panes.cpp's own helper gives (that file's
/// header comment): a flat, one-level search is enough because every button
/// MainComponent/MainComponentSession own has a distinct label and none of
/// them nests inside another container.
juce::Button* findButtonByText(juce::Component& root, const juce::String& text) {
    for (int i = 0; i < root.getNumChildComponents(); ++i) {
        if (auto* button = dynamic_cast<juce::Button*>(root.getChildComponent(i))) {
            if (button->getButtonText() == text) return button;
        }
    }
    return nullptr;
}

}  // namespace

// --- HIGH F1: std::filesystem::path from a juce::File must never round-trip
// through a narrow (UTF-8) std::string on Windows -----------------------

TEST_CASE("toFsPath decodes a juce::File's path exactly, independent of the active code page",
         "[main_component_session]") {
    // "phien do" (Vietnamese, with diacritics outside the CP-1252/1258
    // repertoire -- the exact class of name that corrupts through
    // std::filesystem::path(juce::File::getFullPathName().toStdString()) on
    // a stock Windows box). Compares WIDE CHARACTERS taken directly from
    // JUCE's own internal string, never round-tripped through any narrow
    // encoding -- this means the same thing on every code page.
    const juce::File file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile(juce::CharPointer_UTF8("phi\xE1\xBB\x87n \xC4\x91o"));

    const auto path = toFsPath(file);

    CHECK(path.wstring() == file.getFullPathName().toWideCharPointer());
}

TEST_CASE("toFsPath's implementation never round-trips through toStdString()",
         "[main_component_session]") {
    // Fix round HIGH F1's own mutation is UNCATCHABLE by a value comparison
    // on THIS machine: GetACP() here is 65001 (UTF-8), so
    // std::filesystem::path's narrow-string constructor happens to decode
    // UTF-8 correctly anyway (measured directly while fixing this finding --
    // path(toStdString()) and path(toWideCharPointer()) produced
    // byte-identical wstrings here). That is exactly why every earlier local
    // run of this feature passed despite the bug: CI's windows-latest runs a
    // non-UTF-8 ACP (1252/1258), where the SAME toStdString()-based code
    // corrupts a non-ASCII folder name, and no local re-run of the VALUE test
    // above can ever reproduce that gap on this box.
    //
    // What DOES run identically on every machine is reading JuceFsPath.h's
    // own source and confirming the function body never calls
    // toStdString() -- the same structural-guard shape this codebase already
    // uses for a property a runtime call cannot observe portably
    // (app/tests/CodeLines.h's own header comment; test_spl_drain.cpp's
    // D1/D4 scans). codeText() strips comments first, so this file's own
    // prose ABOUT toStdString() (explaining the bug) does not trip the
    // check -- only a real call in the code would.
    const auto path = std::filesystem::path(__FILE__).parent_path() / ".." / "src" / "JuceFsPath.h";
    const auto text = rta::test::codeText(path);
    CHECK(text.find("tostdstring") == std::string::npos);
    // And the guard itself is not vacuous: the fixed body IS present.
    CHECK(text.find("towidecharpointer") != std::string::npos);
}

TEST_CASE("Save/Open round-trip through a real folder with a Vietnamese name",
         "[main_component_session]") {
    // An end-to-end sanity check that the feature works with such a name on
    // THIS machine (it does not, by itself, distinguish the HIGH F1 bug from
    // the fix -- see the structural guard above for why no real-disk test
    // can do that on an ACP-65001 box).
    const juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("rta-test-mc-session-fixround")
                               .getChildFile(juce::CharPointer_UTF8("phi\xC3\xAAn \xC4\x91o"));
    dir.deleteRecursively();
    dir.createDirectory();
    struct Cleanup {
        juce::File dir;
        ~Cleanup() { dir.deleteRecursively(); }
    } cleanup{dir};

    MainComponent writer;
    writer.setSyntheticMode(true);
    auto& writerLibrary = MainComponentTestAccess::libraryForTest(writer);
    writerLibrary.add(*Trace::make(makeMeta("vn"), std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f}), "VN", "g");

    MainComponentTestAccess::saveSessionForTest(writer, dir);
    CHECK(MainComponentTestAccess::readoutForTest(writer).startsWith("SAVED"));
    // Round-2 R2-2's value-based check: resolved through juce::File (UTF-16
    // internally), never through the narrow std::string this test is
    // specifically trying to stress -- so it discriminates the ACP bug on a
    // CI box whose ACP is NOT 65001 (windows-latest), even though it cannot
    // discriminate anything on THIS box, where GetACP() == 65001 makes the
    // buggy and fixed code paths land on the same bytes (this file's own
    // structural-guard test above explains why in full).
    CHECK(dir.getChildFile("session.index").existsAsFile());

    MainComponent reader;
    reader.setSyntheticMode(true);
    MainComponentTestAccess::openSessionForTest(reader, dir);

    CHECK(MainComponentTestAccess::readoutForTest(reader).startsWith("OPENED"));
    const auto entries = MainComponentTestAccess::libraryForTest(reader).entries();
    REQUIRE(entries.size() == 1u);
    CHECK(entries.front().name == "VN");
}

// --- MEDIUM F3: five claimed behaviours with no RED mutant ------------------

TEST_CASE("SAVE SESSION and OPEN SESSION are wired to a click handler",
         "[main_component_session]") {
    // M6: an unwired button is silently dead -- clicking it does nothing,
    // and nothing else in this suite calls saveClicked()/openClicked()
    // directly, so no other test would notice. A synchronous click through
    // to the (async) chooser is not required here: onClick being SET is
    // exactly what a real click would invoke.
    MainComponent component;
    component.setSyntheticMode(true);

    auto* saveButton = findButtonByText(component, "SAVE SESSION");
    auto* openButton = findButtonByText(component, "OPEN SESSION");
    REQUIRE(saveButton != nullptr);
    REQUIRE(openButton != nullptr);
    CHECK(static_cast<bool>(saveButton->onClick));
    CHECK(static_cast<bool>(openButton->onClick));
}

TEST_CASE("Open syncs the pane selector buttons' toggle state, not just currentPaneView",
         "[main_component_session]") {
    // M8: currentPaneView() is a plain member restoreWorkspaceFromSession
    // sets directly -- it moves even if the paneXButton_.setToggleState(...)
    // lines are deleted, so a test that only reads currentPaneView() (like
    // test_main_component_session.cpp's round-trip test) cannot tell a
    // resynced selector from a stale one. This drives the buttons the
    // operator actually sees.
    TempDir dir("pane-sync");

    MainComponent writer;
    writer.setSyntheticMode(true);
    writer.selectPaneView(PaneSelectorButton::Spl);
    MainComponentTestAccess::saveSessionForTest(writer, juce::File(dir.path.string()));

    MainComponent reader;
    reader.setSyntheticMode(true);
    // Moved away from BOTH the default (Rta) and the saved pane (Spl), so a
    // stale selector is observable regardless of which state it defaults to.
    reader.selectPaneView(PaneSelectorButton::Transfer);

    MainComponentTestAccess::openSessionForTest(reader, juce::File(dir.path.string()));

    auto* rtaButton = findButtonByText(reader, "RTA");
    auto* transferButton = findButtonByText(reader, "TRANSFER");
    auto* splButton = findButtonByText(reader, "SPL");
    REQUIRE(rtaButton != nullptr);
    REQUIRE(transferButton != nullptr);
    REQUIRE(splButton != nullptr);

    CHECK_FALSE(rtaButton->getToggleState());
    CHECK_FALSE(transferButton->getToggleState());
    CHECK(splButton->getToggleState());
}

TEST_CASE("Open restores the XOVER selector button and pane, not just currentPaneView",
         "[main_component_session]") {
    // PR #45 fix round 3 (PR #43 reconciliation checklist item 3):
    // restoreWorkspaceFromSession() (MainComponentPanes.cpp) synced only the
    // RTA/TRANSFER/SPL toggles, unlike selectPaneView() a few lines above it
    // in the same file, which already syncs all four -- opening a saved
    // XOVER session left every selector button dark even though the xover
    // pane itself was showing. Same M8 shape as the test above, the fourth
    // button.
    TempDir dir("xover-pane-sync");

    MainComponent writer;
    writer.setSyntheticMode(true);
    writer.selectPaneView(PaneSelectorButton::Xover);
    MainComponentTestAccess::saveSessionForTest(writer, juce::File(dir.path.string()));

    MainComponent reader;
    reader.setSyntheticMode(true);
    REQUIRE(reader.currentPaneView() == PaneView::Rta);

    MainComponentTestAccess::openSessionForTest(reader, juce::File(dir.path.string()));

    CHECK(reader.currentPaneView() == PaneView::Xover);
    CHECK(dynamic_cast<const rta::view::CrossoverPaneView*>(&MainComponentTestAccess::pane(reader)) !=
         nullptr);

    auto* rtaButton = findButtonByText(reader, "RTA");
    auto* transferButton = findButtonByText(reader, "TRANSFER");
    auto* splButton = findButtonByText(reader, "SPL");
    auto* xoverButton = findButtonByText(reader, "XOVER");
    REQUIRE(rtaButton != nullptr);
    REQUIRE(transferButton != nullptr);
    REQUIRE(splButton != nullptr);
    REQUIRE(xoverButton != nullptr);

    CHECK_FALSE(rtaButton->getToggleState());
    CHECK_FALSE(transferButton->getToggleState());
    CHECK_FALSE(splButton->getToggleState());
    CHECK(xoverButton->getToggleState());
}

TEST_CASE("Open restores the visible flag, not just the trace data",
         "[main_component_session]") {
    // M9: TraceLibrary::add() always adds a trace as visible -- the ONLY
    // thing that can make a restored entry invisible is Open's own
    // setVisible() call. A test that never hides anything before saving
    // cannot tell that call apart from a no-op.
    TempDir dir("visible-flag");

    MainComponent writer;
    writer.setSyntheticMode(true);
    auto& writerLibrary = MainComponentTestAccess::libraryForTest(writer);
    const auto id =
        writerLibrary.add(*Trace::make(makeMeta("hidden"), std::vector<float>(5, -20.0f)), "Hidden", "g");
    REQUIRE_FALSE(id.empty());
    REQUIRE(writerLibrary.setVisible(id, false));

    MainComponentTestAccess::saveSessionForTest(writer, juce::File(dir.path.string()));

    MainComponent reader;
    reader.setSyntheticMode(true);
    MainComponentTestAccess::openSessionForTest(reader, juce::File(dir.path.string()));

    const auto* entry = MainComponentTestAccess::libraryForTest(reader).entry("hidden");
    REQUIRE(entry != nullptr);
    CHECK_FALSE(entry->visible);
}

TEST_CASE("Open reports exactly how many blobs failed to read, and the trace after the bad one still loads",
         "[main_component_session]") {
    // M4, round-2 R2-3 rebuild: the ORIGINAL fixture put the missing blob
    // LAST in index/capture order, so a mutant that changes the read loop's
    // `continue` (after a failed readTrace) to `break` survived -- both stop
    // reading at the same point when the bad entry is last, so nothing here
    // could tell them apart. Three traces, bad blob in the MIDDLE: `continue`
    // goes on to read "last" too; `break` abandons the loop right there and
    // drops "last" as well, which the size==2/"Last" assertions below catch.
    TempDir dir("partial-middle");

    MainComponent writer;
    writer.setSyntheticMode(true);
    auto& writerLibrary = MainComponentTestAccess::libraryForTest(writer);
    writerLibrary.add(*Trace::make(makeMeta("first"), std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f}), "First",
                      "g");
    writerLibrary.add(*Trace::make(makeMeta("middlebad"), std::vector<float>{6.f, 7.f, 8.f, 9.f, 10.f}),
                      "MiddleBad", "g");
    writerLibrary.add(*Trace::make(makeMeta("last"), std::vector<float>{11.f, 12.f, 13.f, 14.f, 15.f}),
                      "Last", "g");
    MainComponentTestAccess::saveSessionForTest(writer, juce::File(dir.path.string()));

    // The index still names "middlebad"; only its blob is gone -- a partial
    // folder (a hand-deleted file, or an interrupted copy), not a corrupt
    // index.
    REQUIRE(std::filesystem::remove(dir.path / "traces" / "middlebad.bin"));

    MainComponent reader;
    reader.setSyntheticMode(true);
    MainComponentTestAccess::openSessionForTest(reader, juce::File(dir.path.string()));

    const auto readout = MainComponentTestAccess::readoutForTest(reader);
    CHECK(readout.startsWith("OPENED"));
    CHECK(readout.contains("1 trace(s) skipped"));

    const auto entries = MainComponentTestAccess::libraryForTest(reader).entries();
    REQUIRE(entries.size() == 2u);
    CHECK(entries[0].name == "First");
    CHECK(entries[1].name == "Last");  // the trace AFTER the bad one -- this is what `break` drops
}

TEST_CASE("Open refuses wholesale when every blob fails, even though the index parsed",
         "[main_component_session]") {
    // M4b: a valid, parseable index naming traces none of which can be read
    // back is a DIFFERENT failure mode than "opening a corrupt index"
    // (test_main_component_session.cpp) -- that case never reaches
    // readTrace() at all. Without this wholesale refusal, Open would proceed
    // to clear() the library and add nothing, silently replacing whatever
    // was there with an empty one that looks exactly like a deliberately
    // empty session.
    TempDir dir("all-blobs-gone");

    MainComponent writer;
    writer.setSyntheticMode(true);
    auto& writerLibrary = MainComponentTestAccess::libraryForTest(writer);
    writerLibrary.add(*Trace::make(makeMeta("only"), std::vector<float>(5, -20.0f)), "Only", "g");
    MainComponentTestAccess::saveSessionForTest(writer, juce::File(dir.path.string()));

    std::filesystem::remove_all(dir.path / "traces");  // index intact; every blob gone

    MainComponent reader;
    reader.setSyntheticMode(true);
    auto& readerLibrary = MainComponentTestAccess::libraryForTest(reader);
    readerLibrary.add(*Trace::make(makeMeta("stale"), std::vector<float>(5, -20.0f)), "Stale", "g");

    MainComponentTestAccess::openSessionForTest(reader, juce::File(dir.path.string()));

    CHECK(MainComponentTestAccess::readoutForTest(reader).startsWith("OPEN FAILED"));
    const auto entries = MainComponentTestAccess::libraryForTest(reader).entries();
    REQUIRE(entries.size() == 1u);
    CHECK(entries.front().name == "Stale");  // untouched -- clear() must not have run
}

// --- MEDIUM F2: an unrecognised saved pane name must be REPORTED ------------

TEST_CASE("Open reports an unrecognised pane name and still falls back to RTA",
         "[main_component_session]") {
    // SessionCodec's own "never refuse a session over a layout word" rule
    // means decodeIndex accepts any [pane] view= string -- resolvePaneView()
    // is what falls back, and PaneRegistry.h says the CALLER reports that
    // fallback. WorkspaceView deliberately does not (its own class comment);
    // this session loader is the seam that owes the report, and until this
    // fix it silently dropped it.
    TempDir dir("unknown-pane");
    {
        std::ofstream index((dir.path / "session.index").string(), std::ios::binary);
        index << "schema=3\n[pane]\nview=nonexistent_pane_type\nweight=1\n";
    }

    MainComponent reader;
    reader.setSyntheticMode(true);

    MainComponentTestAccess::openSessionForTest(reader, juce::File(dir.path.string()));

    const auto readout = MainComponentTestAccess::readoutForTest(reader);
    CHECK(readout.startsWith("OPENED"));
    CHECK(readout.contains("nonexistent_pane_type"));
    CHECK(readout.contains("unknown"));
    CHECK(reader.currentPaneView() == PaneView::Rta);
}
