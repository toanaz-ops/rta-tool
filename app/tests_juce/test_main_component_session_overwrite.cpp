// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests_juce. F6 (docs/HUMAN-QA-QUEUE.md D11, PR #43
// F6): Save had no overwrite prompt -- clicking SAVE onto a folder that
// already held a session silently replaced it. This proves
// `MainComponentSession::maybeConfirmAndSave` (MainComponentSession.cpp)
// against a REAL MainComponent, with the confirmation injected through
// `MainComponentTestAccess::setConfirmOverwriteForTest` so no real, modal
// `juce::AlertWindow` needs a message loop pumped in this offscreen/CI
// environment.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "trace/Trace.h"

#include <juce_core/juce_core.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

using rta::trace::CaptureMeta;
using rta::trace::Trace;

namespace {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char* name)
        : path(std::filesystem::temp_directory_path() / "rta-test-mc-session-overwrite" / name) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

std::string readWholeFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

CaptureMeta makeMeta(std::string id) {
    CaptureMeta m;
    m.id = std::move(id);
    m.sampleRate = 48000.0;
    m.fftSize = 8;  // pointCountFor(8) == 5, matching the 5-point vector below
    return m;
}

}  // namespace

TEST_CASE("Save into a folder with an existing session.index asks first, via the injected confirm",
         "[main_component_session_overwrite]") {
    TempDir dir("existing-refused");
    const auto indexPath = dir.path / "session.index";

    MainComponent writer;
    writer.setSyntheticMode(true);
    // Seed an existing session the ordinary way (no prompt involved: this is
    // the FIRST save, into a folder that started empty).
    MainComponentTestAccess::saveSessionForTest(writer, juce::File(dir.path.string()));
    REQUIRE(MainComponentTestAccess::readoutForTest(writer).startsWith("SAVED"));
    REQUIRE(std::filesystem::exists(indexPath));
    const auto originalBytes = readWholeFile(indexPath);

    // Something a second, unconfirmed save WOULD change if it ran -- proves
    // "unchanged" below is not just "nothing to change anyway".
    auto& library = MainComponentTestAccess::libraryForTest(writer);
    library.add(*Trace::make(makeMeta("would-be-saved"), std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f}),
               "Should not reach disk", "g");

    bool confirmCalled = false;
    MainComponentTestAccess::setConfirmOverwriteForTest(
        writer, [&confirmCalled](const juce::File&, std::function<void(bool)> respond) {
            confirmCalled = true;
            respond(false);  // operator clicks Cancel
        });

    MainComponentTestAccess::maybeConfirmAndSaveForTest(writer, juce::File(dir.path.string()));

    CHECK(confirmCalled);
    // Refused: performSave() never ran, so the file on disk is byte-for-byte
    // what it was before this second attempt -- it does NOT carry the trace
    // added above. This is the mutant this test exists to catch (F6's own
    // "skip the check"): a `maybeConfirmAndSave` that calls performSave()
    // regardless of `respond`'s value would overwrite the file here anyway.
    CHECK(readWholeFile(indexPath) == originalBytes);
}

TEST_CASE("Save into a folder with an existing session.index writes once the prompt is accepted",
         "[main_component_session_overwrite]") {
    TempDir dir("existing-accepted");
    const auto indexPath = dir.path / "session.index";

    MainComponent writer;
    writer.setSyntheticMode(true);
    MainComponentTestAccess::saveSessionForTest(writer, juce::File(dir.path.string()));
    REQUIRE(MainComponentTestAccess::readoutForTest(writer).startsWith("SAVED"));
    const auto originalBytes = readWholeFile(indexPath);

    auto& library = MainComponentTestAccess::libraryForTest(writer);
    library.add(*Trace::make(makeMeta("added-before-accept"), std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f}),
               "Added before accept", "g");

    bool confirmCalled = false;
    MainComponentTestAccess::setConfirmOverwriteForTest(
        writer, [&confirmCalled](const juce::File&, std::function<void(bool)> respond) {
            confirmCalled = true;
            respond(true);  // operator clicks Overwrite
        });

    MainComponentTestAccess::maybeConfirmAndSaveForTest(writer, juce::File(dir.path.string()));

    CHECK(confirmCalled);
    CHECK(MainComponentTestAccess::readoutForTest(writer).startsWith("SAVED"));
    // Accepted: performSave() DID run again, and the newly-added trace is
    // now part of what is on disk -- the file differs from the pre-accept
    // bytes.
    CHECK(readWholeFile(indexPath) != originalBytes);

    // Independent confirmation, through the real read path rather than a
    // byte comparison: a fresh reader opening this folder now sees the trace
    // that was added AFTER the first save.
    MainComponent reader;
    reader.setSyntheticMode(true);
    MainComponentTestAccess::openSessionForTest(reader, juce::File(dir.path.string()));
    const auto* reopenedTrace = MainComponentTestAccess::libraryForTest(reader).trace("added-before-accept");
    CHECK(reopenedTrace != nullptr);
}

TEST_CASE("Save into a folder with no session.index never asks -- writes straight through",
         "[main_component_session_overwrite]") {
    TempDir dir("empty-folder");  // created but never saved into

    MainComponent writer;
    writer.setSyntheticMode(true);

    bool confirmCalled = false;
    MainComponentTestAccess::setConfirmOverwriteForTest(
        writer, [&confirmCalled](const juce::File&, std::function<void(bool)> respond) {
            confirmCalled = true;
            respond(true);
        });

    MainComponentTestAccess::maybeConfirmAndSaveForTest(writer, juce::File(dir.path.string()));

    // The mutant this half catches: a `maybeConfirmAndSave` that always asks,
    // even for a folder with nothing in it yet.
    CHECK_FALSE(confirmCalled);
    CHECK(MainComponentTestAccess::readoutForTest(writer).startsWith("SAVED"));
    CHECK(std::filesystem::exists(dir.path / "session.index"));
}
