// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests_juce. Proves SAVE SESSION / OPEN SESSION
// against a REAL MainComponent (the same reasoning test_main_component_panes.cpp
// gives for its own suite): SessionStore/SessionCodec were built and unit-
// tested in lane L5a with no caller anywhere in app/src, so what only a real
// MainComponent can prove is that a click actually reaches them, that Open
// really replaces the live TraceLibrary and rebuilds the workspace, and that
// a refusal leaves both untouched.
//
// The FileChooser dialog itself is never driven in these tests -- performSave/
// performOpen (MainComponentSession.h/.cpp) are exactly what its completion
// callback invokes, and MainComponentTestAccess exposes them directly for the
// same "drive the real seam a click uses, without a message loop pumping an
// OS dialog" reason MainComponentTestAccess already gives for its other
// members.
//
// Threading note (task brief item 5, "Save while a trace is being captured
// does not tear it"): TraceLibrary has no lock and no atomic state -- it is
// designed to be touched from the message thread only, and every call this
// suite makes into it (through MainComponentTestAccess::mutableLibrary, or
// through performSave/performOpen themselves) runs synchronously on the
// thread that calls it, exactly as a real button click would. Nothing in
// app/src today ever adds a trace to a library from any OTHER thread --
// grep confirms TraceLibrary::add has no caller outside this test file, the
// two test_trace_library.cpp fixtures and MainComponentSession.cpp itself,
// all message-thread callers -- so a capture "in progress" is never a
// concurrent writer Save could race: JUCE's message loop runs one callback
// at a time, so a click on SAVE SESSION and a future "keep this capture"
// click can never execute their bodies concurrently by construction. Should
// a background capture thread ever gain its own write access to a
// TraceLibrary, THAT change is what would need to add the locking this
// class deliberately does not carry today.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "trace/Trace.h"
#include "view/SplView.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using rta::trace::CaptureMeta;
using rta::trace::Field;
using rta::trace::Trace;
using rta::view::PaneSelectorButton;
using rta::view::PaneView;
using rta::view::SplView;

namespace {

/// Removes itself on destruction, the same shape every other TempDir fixture
/// in this tree already uses (test_main_component_panes.cpp, test_session_store.cpp).
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char* name)
        : path(std::filesystem::temp_directory_path() / "rta-test-mc-session" / name) {
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

}  // namespace

TEST_CASE("Open restores the library and pane view a matching Save wrote",
         "[main_component_session]") {
    TempDir dir("roundtrip");

    MainComponent writer;
    writer.setSyntheticMode(true);
    writer.selectPaneView(PaneSelectorButton::Spl);

    auto& writerLibrary = MainComponentTestAccess::mutableLibrary(writer);
    writerLibrary.add(*Trace::make(makeMeta("t1"), std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f}), "FOH",
                      "positions");
    writerLibrary.add(*Trace::make(makeMeta("t2"), std::vector<float>{6.f, 7.f, 8.f, 9.f, 10.f}), "Delay",
                      "positions");

    MainComponentTestAccess::saveSession(writer, juce::File(dir.path.string()));
    CHECK(MainComponentTestAccess::sessionReadout(writer).startsWith("SAVED"));

    MainComponent reader;
    reader.setSyntheticMode(true);
    // Reader starts on RTA (the default) -- Open must move it to SPL, proving
    // the loaded session's pane spec wins over whatever was already showing.
    REQUIRE(reader.currentPaneView() == PaneView::Rta);

    MainComponentTestAccess::openSession(reader, juce::File(dir.path.string()));

    CHECK(MainComponentTestAccess::sessionReadout(reader).startsWith("OPENED"));

    const auto entries = MainComponentTestAccess::library(reader).entries();
    REQUIRE(entries.size() == 2u);
    std::vector<std::string> names;
    for (const auto& e : entries) names.push_back(e.name);
    std::sort(names.begin(), names.end());
    CHECK(names == std::vector<std::string>{"Delay", "FOH"});

    const auto* t1 = MainComponentTestAccess::library(reader).trace("t1");
    REQUIRE(t1 != nullptr);
    const auto mag = t1->field(Field::Magnitude);
    REQUIRE(mag.size() == 5u);
    // Bit-exact: TraceBlobCodec stores a raw float32 array (memory/
    // float32-fft-precision.md), so a value that was never itself computed
    // by an FFT round-trips with no tolerance needed at all.
    CHECK(mag[2] == 3.0f);

    CHECK(reader.currentPaneView() == PaneView::Spl);
    CHECK(dynamic_cast<const SplView*>(&MainComponentTestAccess::pane(reader)) != nullptr);
}

TEST_CASE("opening a missing folder refuses and leaves the library untouched",
         "[main_component_session]") {
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::mutableLibrary(component);
    library.add(*Trace::make(makeMeta("keep"), std::vector<float>(5, -20.0f)), "Keep", "g");

    TempDir dir("missing");
    std::filesystem::remove_all(dir.path);  // the folder itself must not exist

    MainComponentTestAccess::openSession(component, juce::File(dir.path.string()));

    CHECK(MainComponentTestAccess::sessionReadout(component).startsWith("OPEN FAILED"));
    const auto entries = MainComponentTestAccess::library(component).entries();
    REQUIRE(entries.size() == 1u);
    CHECK(entries.front().name == "Keep");
}

TEST_CASE("opening a corrupt index refuses and leaves the library untouched",
         "[main_component_session]") {
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::mutableLibrary(component);
    library.add(*Trace::make(makeMeta("keep"), std::vector<float>(5, -20.0f)), "Keep", "g");

    TempDir dir("corrupt");
    {
        // decodeIndex's own "garbage decodes to Malformed" case
        // (test_session_codec.cpp) -- reused here against the real file path
        // rather than the codec function directly.
        std::ofstream bad((dir.path / "session.index").string(), std::ios::binary);
        bad << "not an index at all";
    }

    MainComponentTestAccess::openSession(component, juce::File(dir.path.string()));

    CHECK(MainComponentTestAccess::sessionReadout(component).startsWith("OPEN FAILED"));
    const auto entries = MainComponentTestAccess::library(component).entries();
    REQUIRE(entries.size() == 1u);
    CHECK(entries.front().name == "Keep");
}

TEST_CASE("a session with no captures still opens, to an empty library",
         "[main_component_session]") {
    // Distinguishes "the index parsed but named nothing" (a legitimately
    // empty session -- must open) from "every named trace failed to read"
    // (a partial folder -- must refuse). Both look like "zero traces loaded"
    // from the outside; only the readout and the untouched-vs-replaced
    // library tell them apart.
    TempDir dir("empty-session");

    MainComponent writer;
    writer.setSyntheticMode(true);
    MainComponentTestAccess::saveSession(writer, juce::File(dir.path.string()));  // no traces added

    MainComponent reader;
    reader.setSyntheticMode(true);
    auto& readerLibrary = MainComponentTestAccess::mutableLibrary(reader);
    readerLibrary.add(*Trace::make(makeMeta("stale"), std::vector<float>(5, -20.0f)), "Stale", "g");

    MainComponentTestAccess::openSession(reader, juce::File(dir.path.string()));

    CHECK(MainComponentTestAccess::sessionReadout(reader).startsWith("OPENED"));
    CHECK(MainComponentTestAccess::library(reader).entries().empty());
}
