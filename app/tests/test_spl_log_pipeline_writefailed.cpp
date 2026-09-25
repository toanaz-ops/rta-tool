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
