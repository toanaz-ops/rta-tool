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
#include <filesystem>
#include <fstream>
#include <sstream>

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
