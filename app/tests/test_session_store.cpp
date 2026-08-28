// SPDX-License-Identifier: AGPL-3.0-or-later
#include "trace/SessionStore.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace rta::trace;

namespace {
/// A directory that removes itself, so a failing assertion cannot leave the
/// next run reading a previous run's session.
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char* name)
        : path(std::filesystem::temp_directory_path() / "rta-test" / name) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

CaptureMeta meta(std::string id) {
    CaptureMeta m;
    m.id = std::move(id);
    m.sampleRate = 48000.0;
    m.fftSize = 8;
    return m;
}
}  // namespace

TEST_CASE("a session round-trips through the filesystem", "[store]") {
    TempDir dir("roundtrip");
    SessionStore store(dir.path);

    SessionDocument doc;
    doc.captures.push_back(meta("t1"));
    doc.entries.push_back(LibraryEntry{"t1", "FOH", "positions", 0, true});
    REQUIRE(store.writeIndex(doc) == StoreStatus::Ok);

    SessionDocument back;
    REQUIRE(store.readIndex(back) == StoreStatus::Ok);
    CHECK(back.captures.size() == 1u);
    CHECK(back.entries.front().name == "FOH");
}

TEST_CASE("a missing index is NotFound, not Malformed", "[store]") {
    TempDir dir("missing");
    SessionStore store(dir.path);
    SessionDocument back;
    CHECK(store.readIndex(back) == StoreStatus::NotFound);
}

TEST_CASE("an interrupted write leaves the previous index intact", "[store]") {
    TempDir dir("atomic");
    SessionStore store(dir.path);

    SessionDocument first;
    first.captures.push_back(meta("t1"));
    REQUIRE(store.writeIndex(first) == StoreStatus::Ok);

    // Simulate a crash mid-write: a stray temp file beside a good index.
    { std::ofstream stray(dir.path / "session.index.tmp"); stray << "half writ"; }

    SessionDocument back;
    REQUIRE(store.readIndex(back) == StoreStatus::Ok);
    CHECK(back.captures.size() == 1u);
    CHECK(back.captures.front().id == "t1");
}

TEST_CASE("writeIndex consumes a pre-existing .tmp rather than leaving it untouched", "[store]") {
    TempDir dir("atomic-write");
    SessionStore store(dir.path);

    // Plant a stray tmp file BEFORE the write this time -- unlike the test
    // above, which checks what readIndex does with a leftover tmp AFTER a
    // successful write. This one aims at writeIndex itself: a
    // truncate-and-write-directly-to-session.index implementation (no tmp
    // file at all) would never open or touch session.index.tmp, so this
    // garbage would still be sitting there completely unmodified afterward.
    // A real tmp+rename writer necessarily opens session.index.tmp for
    // writing and then consumes it via rename -- the garbage cannot survive
    // either step. This cannot stage an actual crash mid-write (no unit test
    // can kill the process at that instant), but it does distinguish the two
    // implementations by the trace atomicity leaves behind.
    const auto tmpPath = dir.path / "session.index.tmp";
    { std::ofstream stray(tmpPath, std::ios::binary); stray << "GARBAGE-PRE-EXISTING-TMP"; }

    SessionDocument doc;
    doc.captures.push_back(meta("t9"));
    REQUIRE(store.writeIndex(doc) == StoreStatus::Ok);

    // The rename step of a tmp+rename writer removes the source path, so the
    // tmp should be gone entirely -- not merely holding different bytes.
    CHECK_FALSE(std::filesystem::exists(tmpPath));

    SessionDocument back;
    REQUIRE(store.readIndex(back) == StoreStatus::Ok);
    CHECK(back.captures.size() == 1u);
    CHECK(back.captures.front().id == "t9");
}

TEST_CASE("a trace blob round-trips through traces/", "[store]") {
    TempDir dir("blob");
    SessionStore store(dir.path);

    auto t = Trace::make(meta("t7"), std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f});
    REQUIRE(t.has_value());
    REQUIRE(store.writeTrace(*t) == StoreStatus::Ok);

    std::optional<Trace> back;
    REQUIRE(store.readTrace(meta("t7"), back) == StoreStatus::Ok);
    REQUIRE(back.has_value());
    CHECK(back->field(Field::Magnitude)[2] == 3.0f);
    CHECK_FALSE(back->has(Field::Phase));
}

TEST_CASE("a newer schema on disk is refused", "[store]") {
    TempDir dir("newer");
    SessionStore store(dir.path);
    SessionDocument doc;
    doc.schemaVersion = kSchemaVersion + 1;
    REQUIRE(store.writeIndex(doc) == StoreStatus::Ok);

    SessionDocument back;
    CHECK(store.readIndex(back) == StoreStatus::NewerSchema);
}
