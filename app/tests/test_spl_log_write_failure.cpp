// SPDX-License-Identifier: AGPL-3.0-or-later
// Station-4 fix round, PR #31, round 3, verifier finding 2 (MEDIUM): a
// mid-session write failure -- the stream opened fine, then a LATER write()
// call fails (the disk fills, or the handle is closed underneath this
// writer) -- used to be invisible. writeFailed_ only ever got set inside
// openSegment(), so a writer that opened its file successfully and later
// started failing to write kept reporting writeFailed() == false forever.
//
// New file rather than growing test_spl_log.cpp (395 lines, close to the
// project's 400-line hard cap already) -- the coordinator's own instruction
// this round.
#include "export/SplLog.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

using namespace rta::splexport;
using rta::measure::SplConfig;
using rta::meter::Block;

namespace {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char* name)
        : path(std::filesystem::temp_directory_path() / "rta-test-spllog-writefail" / name) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

Block blockAtLevel(std::uint64_t index, std::uint32_t samples, double levelDb) {
    Block b;
    b.blockIndex = index;
    b.blockSamples = samples;
    b.sumSquares = static_cast<double>(samples) * std::pow(10.0, levelDb / 10.0);
    return b;
}

SplLogHeaderInfo headerInfo() {
    SplLogHeaderInfo info;
    info.weighting = rta::dsp::WeightingType::A;
    info.detector = rta::meter::TimeWeighting::Fast;
    info.blockSamples = 48000;
    info.sampleRate = 48000.0;
    info.startedAtUnixMs = 1'700'000'000'000ull;
    return info;
}

}  // namespace

TEST_CASE("a write() failure AFTER a successful open sets writeFailed(), "
         "and stays set",
         "[spl_log_write_failure]") {
    TempDir dir("mid-session");
    SplConfig config;
    // segmentBlocks large enough that nothing here rotates -- the property
    // under test is a write failing WITHOUT a rotation involved, isolating
    // it from openSegment()'s own, already-covered failure path
    // (test_spl_log.cpp's "openSegment cannot open" case).
    SplLogWriter writer((dir.path / "channel").string(), config, headerInfo(), 3600);
    REQUIRE_FALSE(writer.writeFailed());

    writer.write(blockAtLevel(0, 48000, 80.0));
    CHECK_FALSE(writer.writeFailed());  // a normal write reports no failure

    // Force the ALREADY-OPEN stream into a failed state -- portable across
    // every CI OS, unlike simulating a full disk or reaching into an
    // implementation's native file handle (SplLog.h's own comment on this
    // test-only seam).
    writer.forceStreamFailureForTest();
    writer.write(blockAtLevel(1, 48000, 80.0));
    CHECK(writer.writeFailed());

    // Sticky: a later write against the now-failed stream (still failed;
    // nothing here re-opens it) must not un-report the earlier failure.
    writer.write(blockAtLevel(2, 48000, 80.0));
    CHECK(writer.writeFailed());
}

// Platform split (PR #55; ubuntu-latest CI RED, then verifier round 1): the
// fixture is a byte string that is NOT well-formed UTF-8. It only THROWS on
// Windows, where utf8Path() decodes UTF-8 into the native wide-char path and
// MSVC's transcode rejects it -- that is the throw D6 catches. It does not
// throw anywhere else, and the three OSes disagree about what happens
// instead:
//   - Linux (libstdc++ bits/fs_path.h): a path IS a byte string, char8_t
//     sequences are copied as bytes with no decode step, and 0x80 is a legal
//     ext4 filename byte -- open() simply succeeds.
//   - macOS: APFS rejects a name that is not valid UTF-8, so open() fails
//     cleanly and openSegment() takes its existing `!is_open()` branch. That
//     is why macOS CI passed the original Windows-shaped assertion -- by
//     accident, through a different branch than the one D6 adds, not
//     because anything threw.
// So the `writeFailed()` assertions are Windows-only (#ifdef _WIN32), the
// Windows assertion is not weakened, and the case that must hold on EVERY OS
// -- segmentPaths() stays non-empty and rotation stays bounded whichever way
// the open went -- carries no platform-conditional expectation.
#ifdef _WIN32
TEST_CASE("a malformed path caught in openSegment sets writeFailed() instead of terminating the process",
         "[spl_log_write_failure]") {
    // D6 (HUMAN-QA-QUEUE): utf8Path() (SplLog.h) throws on ill-formed UTF-8,
    // and openSegment() -- called from the CONSTRUCTOR, on the writer
    // thread, not the message thread any MainComponent-level try/catch
    // guards -- used to call it uncaught. Nothing this app actually produces
    // reaches that throw today (basePath_ always descends from a
    // well-formed-UTF-8 juce::File path); this plants a bare UTF-8
    // continuation byte (0x80, never valid on its own, with no lead byte in
    // front of it) directly into basePath_ to force it anyway. If the catch
    // this fix round adds were removed, the exception would unwind straight
    // out of the constructor above and crash this whole test binary --
    // itself the RED signature the "remove the catch" mutant produces, since
    // there is no surviving assertion to fail against a dead process.
    TempDir dir("malformed-path");
    std::string basePath = (dir.path / "channel").string();
    basePath += '\x80';

    SplConfig config;
    SplLogWriter writer(basePath, config, headerInfo(), 3600);
    CHECK(writer.writeFailed());
    // SplLogPipeline::setupWriters() calls segmentPaths().back() straight
    // after construction (SplLogPipeline.cpp:87): an empty vector there is UB.
    CHECK(writer.segmentPaths().size() == 1u);

    // The writer must stay usable (no crash) for whatever the caller does
    // next -- a write against the never-opened stream is the same silent
    // no-op an unwritable directory already produces two lines above
    // openSegment()'s catch.
    writer.write(blockAtLevel(0, 48000, 80.0));
    CHECK(writer.writeFailed());
}
#endif

TEST_CASE("a path whose open fails or throws still records every segment, and rotation stays bounded",
         "[spl_log_write_failure]") {
    // PR #55 verifier MEDIUM. openSegment()'s early `return` in the catch
    // skipped `segmentPaths_.push_back` and `blocksInSegment_ = 0`. Portable:
    // the same bad-byte basePath fails to open on Windows (throw, caught) and
    // macOS (APFS refuses), and opens fine on Linux -- in every case
    // openSegment() must record the segment and reset its block counter.
    //
    // Closed form: segmentBlocks = 2 rotates at the start of writes 3, 5, 7,
    // so n writes give 1 + floor((n - 1) / 2) segments -- n = 7 -> 4. With
    // the early return this read 0: nothing recorded at construction or on
    // any of the three rotations, and blocksInSegment_ never reset, so every
    // write from the third on re-entered openSegment().
    TempDir dir("rotation-bounded");
    std::string basePath = (dir.path / "channel").string();
    basePath += '\x80';

    SplConfig config;
    SplLogWriter writer(basePath, config, headerInfo(), 2);
    REQUIRE(writer.segmentPaths().size() == 1u);  // .back() is safe, as setupWriters() needs

    constexpr std::uint64_t kWrites = 7;
    for (std::uint64_t i = 0; i < kWrites; ++i) writer.write(blockAtLevel(i, 48000, 80.0));

    CHECK(writer.segmentPaths().size() == 1u + (kWrites - 1) / 2);
    // Every recorded segment path is distinct: segmentIndex_ advanced once
    // per rotation, not once per write.
    std::set<std::string> distinct(writer.segmentPaths().begin(), writer.segmentPaths().end());
    CHECK(distinct.size() == writer.segmentPaths().size());
#ifdef _WIN32
    CHECK(writer.writeFailed());  // Windows: every open threw
#endif
}
