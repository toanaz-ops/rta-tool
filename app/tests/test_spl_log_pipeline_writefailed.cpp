// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a task W2-E2a. Split out of test_spl_log_pipeline.cpp (round 4, PR
// #31, LOW finding 3 -- that file was at 428 lines, over the 400-line hard
// cap): every case here is about ONE subject, the writeFailed() mirror and
// where the file I/O that can trip it actually runs -- station-4 fix round
// findings 4 and 6, plus round 4's own finding 1 (mutant M4). The round-trip
// / rotation / overflow / no-sink cases test_spl_log_pipeline.cpp keeps are a
// different subject (the queue and the writer thread's basic mechanics).
#include "export/SplLogPipeline.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

using namespace rta::splexport;
using rta::measure::SplConfig;
using rta::meter::Block;

namespace {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char* name)
        : path(std::filesystem::temp_directory_path() / "rta-test-spllogpipeline-writefailed" /
               name) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

Block distinguishableBlock(std::uint64_t index) {
    Block b;
    b.blockIndex = index;
    b.blockSamples = 480;
    b.sumSquares = static_cast<double>(b.blockSamples) *
                   std::pow(10.0, (30.0 + static_cast<double>(index % 1000) * 0.05) / 10.0);
    return b;
}

SplLogChannelSpec channelSpec(const std::filesystem::path& dir, int channel) {
    SplLogChannelSpec spec;
    spec.channel = channel;
    spec.basePath = (dir / ("ch" + std::to_string(channel))).string();
    spec.info.weighting = rta::dsp::WeightingType::A;
    spec.info.detector = rta::meter::TimeWeighting::Fast;
    spec.info.blockSamples = 480;
    spec.info.sampleRate = 48000.0;
    spec.info.startedAtUnixMs = 1'700'000'000'000ull;
    return spec;
}

}  // namespace

// --- station-4 fix round (PR #31, finding 4): enable() does no file I/O ---

TEST_CASE("enable() opens the log file on the writer thread, never on the "
         "calling thread",
         "[spl_log_pipeline]") {
    // enable() USED to construct SplLogWriter (which opens the file and
    // writes the CSV/session header) inline, on whatever thread called it --
    // the analysis thread in production, which repo CLAUDE.md's real-time
    // rule forbids doing file I/O on. setWriterFactoryForTest lets this test
    // see WHICH thread is running at the exact moment that construction
    // happens, without needing a fake SplLogWriter: the factory still builds
    // a real one, it just records std::this_thread::get_id() first.
    TempDir dir("thread-id");

    SplLogEnableParams params;
    params.config = SplConfig{};
    params.channels = { channelSpec(dir.path, 0) };

    SplLogPipeline pipeline;
    std::atomic<bool> captured{ false };
    std::thread::id writerThreadId{};
    pipeline.setWriterFactoryForTest(
        [&](const std::string& basePath, const SplConfig& config,
            const rta::splexport::SplLogHeaderInfo& info, std::uint64_t segmentBlocks) {
            writerThreadId = std::this_thread::get_id();
            captured.store(true, std::memory_order_release);
            return std::make_unique<rta::splexport::SplLogWriter>(basePath, config, info,
                                                                   segmentBlocks);
        });

    const auto callingThreadId = std::this_thread::get_id();
    pipeline.enable(params);

    // Poll rather than sleep-then-check: setupWriters() runs as the writer
    // thread's very first act, before any idle sleep, so this is expected to
    // resolve in well under a millisecond -- 500 ms is generous headroom
    // over that, matching the poll-with-bounded-timeout shape this repo
    // already uses for cross-thread handoffs (test_capture_timeout.cpp's own
    // precedent), so a hang shows up as a clear test FAILURE, not the test
    // process blocking forever.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (!captured.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(captured.load(std::memory_order_acquire));
    CHECK(writerThreadId != callingThreadId);

    pipeline.disable();
}

// --- station-4 fix round (PR #31, finding 6): the pipeline mirrors a real
// open failure through writeFailed(), and remembers it past disable() ------

TEST_CASE("writeFailed() is true when the channel's directory does not "
         "exist, and survives disable()",
         "[spl_log_pipeline]") {
    // Same "point basePath into a directory that was never created" idiom
    // test_spl_log.cpp's own openFailed() case uses -- portable across CI
    // OSes, unlike a chmod-based unwritable directory.
    const auto missingDir = std::filesystem::temp_directory_path() /
                            "rta-test-spllogpipeline-writefailed" / "does-not-exist-6a2f9";
    std::filesystem::remove_all(missingDir);

    SplLogEnableParams params;
    params.config = SplConfig{};
    params.channels = { channelSpec(missingDir, 0) };

    SplLogPipeline pipeline;
    pipeline.enable(params);

    // Poll rather than sleep-then-check, same shape as the thread-id test
    // above: setupWriters() (which is where the failing open happens) runs
    // as the writer thread's very first act.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (!pipeline.writeFailed(0) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(pipeline.writeFailed(0));

    pipeline.disable();
    // lastWriteFailed_'s whole reason to exist (SplLogPipeline.h's own
    // comment): a caller reading AFTER the session ended, same shape as
    // droppedBlocks()'s post-disable() snapshot.
    CHECK(pipeline.writeFailed(0));

    std::error_code ec;
    std::filesystem::remove_all(missingDir, ec);
}

// --- round 4, MEDIUM finding 1: mutant M4 -- drainOnce()'s OWN check, not
// just setupWriters()'s, must mirror a write failure ------------------------

TEST_CASE("a write failure that only manifests on write(), never at open, is "
         "caught by the drain -- not just by setup",
         "[spl_log_pipeline]") {
    // Deleting SplLogPipeline.cpp's drainOnce() check (the one right after
    // the inner while-loop writes whatever the ring held) left every
    // existing SPL case green: the "directory does not exist" fixture above
    // fails at OPEN, which setupWriters()'s OWN check (SplLogPipeline.cpp
    // :84-85) already catches -- so that test's outcome does not depend on
    // drainOnce()'s check at all. This one forces the failure into the
    // STREAM directly, AFTER a real, successful open, so setupWriters()'s
    // check sees writeFailed()==false (correctly -- SplLogWriter itself has
    // not written anything yet, so nothing has failed from ITS point of
    // view) and the transition to true can only ever be observed by
    // drainOnce()'s own check, once write() actually runs against the
    // broken stream and sets the writer's own flag.
    TempDir dir("drain-catches-write-failure");

    SplLogEnableParams params;
    params.config = SplConfig{};
    params.channels = { channelSpec(dir.path, 0) };  // a real, writable directory: open succeeds

    SplLogPipeline pipeline;
    pipeline.setWriterFactoryForTest(
        [](const std::string& basePath, const SplConfig& config,
           const rta::splexport::SplLogHeaderInfo& info, std::uint64_t segmentBlocks) {
            auto writer = std::make_unique<rta::splexport::SplLogWriter>(basePath, config, info,
                                                                         segmentBlocks);
            // forceStreamFailureForTest() only breaks the underlying
            // std::ofstream -- it does NOT set writeFailed_ itself (SplLog.h's
            // own comment: that flag is set by write()'s own post-flush
            // check). So the writer this factory hands back still reports
            // writeFailed()==false right now, exactly like a healthy one.
            writer->forceStreamFailureForTest();
            return writer;
        });
    pipeline.enable(params);

    pipeline.pushBlock(0, distinguishableBlock(0));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (!pipeline.writeFailed(0) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(pipeline.writeFailed(0));

    pipeline.disable();
}

// --- LOW follow-up batch, item 10: mutant M6 -- a ROTATION-time openSegment()
// failure, distinct from the M4 case above (which never rotates: a broken
// stream forced before any write, at segment 0) ----------------------------

TEST_CASE("a write failure at ROTATION to a later segment is caught by the drain",
          "[spl_log_pipeline]") {
    // The M4 case above proves drainOnce()'s check against a failure that
    // happens at the FIRST write, with no rotation ever occurring --
    // `SplLogWriter::write()`'s own `if (!stream_) writeFailed_ = true;`
    // branch. This proves the SAME drainOnce() check against the OTHER
    // branch that can set `writeFailed_`: `openSegment()`'s failed-`open()`
    // check (SplLogWriter.cpp), reached from `write()`'s rotation path
    // (`blocksInSegment_ >= segmentBlocks_`) rather than from the
    // constructor's own first call -- `setupWriters()`'s check (which the
    // "directory does not exist" fixture above depends on) only ever sees the
    // FIRST segment's open, so it cannot be what catches this.
    //
    // Portable, no chmod/ACL trick needed: `SplLogWriter` names segment 1 as
    // exactly `<basePath>.gen0.seg1.csv` (SplLogWriter.cpp's own
    // `openSegment()`), so pre-creating THAT PATH AS A DIRECTORY makes
    // `std::ofstream::open()` fail on it -- on every filesystem this project
    // ships on -- while segment 0 (a different path) opens and writes fine.
    TempDir dir("rotation-write-failure");

    const std::string basePath = (dir.path / "ch0").string();
    const std::string seg1Path = basePath + ".gen0.seg1.csv";
    REQUIRE(std::filesystem::create_directory(seg1Path));

    SplLogEnableParams params;
    params.config = SplConfig{};
    params.config.segmentBlocks = 1;  // rotate after exactly one block
    params.channels = { channelSpec(dir.path, 0) };

    SplLogPipeline pipeline;
    pipeline.enable(params);

    // Block 0 fills segment 0 (segmentBlocks == 1); block 1 is what triggers
    // `write()`'s rotation to segment 1, where the pre-created directory
    // makes `openSegment()` fail.
    pipeline.pushBlock(0, distinguishableBlock(0));
    pipeline.pushBlock(0, distinguishableBlock(1));

    const auto rotationDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (!pipeline.writeFailed(0) && std::chrono::steady_clock::now() < rotationDeadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(pipeline.writeFailed(0));

    pipeline.disable();
    std::error_code ec;
    std::filesystem::remove_all(seg1Path, ec);
}

// --- LOW follow-up batch, item 10: mutant M7 -- enable()'s lastWriteFailed_
// reset, for a channel the NEW session does not even touch ------------------

TEST_CASE("enable() clears a previous session's writeFailed for a channel the "
         "new session does not include",
         "[spl_log_pipeline]") {
    // WHY THIS SHAPE, and not "the same channel, re-enabled": when the new
    // session's `params.channels` DOES include the channel, `enable()`
    // synchronously replaces `sinks_[channel]` with a brand new `ChannelSink`
    // (default `writeFailed == false`) before it returns -- so `writeFailed()`
    // reads that fresh sink, never `lastWriteFailed_`, and the mutant this
    // proves against (dropping `enable()`'s
    // `for (auto& failed : lastWriteFailed_) failed.store(false, ...);` loop)
    // would be INVISIBLE to that case. The mutant is only observable for a
    // channel the new session leaves untouched: `sinks_[channel]` then stays
    // null (reset by the PREVIOUS session's own `writerLoop()` shutdown), so
    // `writeFailed()` falls back to `lastWriteFailed_` -- exactly the stale
    // flag `enable()`'s reset exists to clear (SplLogPipeline.h's own
    // comment: "a fresh log's file has not been attempted yet, so it starts
    // clean regardless of whether the PREVIOUS session ever failed").
    const auto missingDir = std::filesystem::temp_directory_path() /
                            "rta-test-spllogpipeline-writefailed" / "does-not-exist-6a2f9-m7";
    std::filesystem::remove_all(missingDir);
    TempDir freshDir("m7-fresh");

    SplLogPipeline pipeline;

    // Session 1: channel 0 fails to open (directory never created).
    SplLogEnableParams failing;
    failing.config = SplConfig{};
    failing.channels = { channelSpec(missingDir, 0) };
    pipeline.enable(failing);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (!pipeline.writeFailed(0) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(pipeline.writeFailed(0));
    pipeline.disable();
    REQUIRE(pipeline.writeFailed(0));  // the post-disable snapshot, as above

    // Session 2: a DIFFERENT channel, a real writable directory. Channel 0 is
    // absent from `channels` entirely.
    SplLogEnableParams fresh;
    fresh.config = SplConfig{};
    fresh.channels = { channelSpec(freshDir.path, 1) };
    pipeline.enable(fresh);

    // THE FIX, checked the instant enable() returns: the reset loop runs
    // synchronously on the calling thread, before the writer thread is even
    // started, so this needs no poll.
    CHECK_FALSE(pipeline.writeFailed(0));

    pipeline.disable();
}
