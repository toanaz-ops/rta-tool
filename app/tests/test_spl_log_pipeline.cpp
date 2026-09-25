// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a task W2-E2a (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md's
// amendment "W2-E -- the wiring nobody was assigned"; record docs/dsp/
// 2026-09-16-spl-pro-l6a.md §10, §13 Q7): the queue + writer thread half,
// exercised with no AnalysisThread and no JUCE in the path.
#include "export/SplLogPipeline.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace rta::splexport;
using rta::measure::SplConfig;
using rta::meter::Block;

namespace {

// Same TempDir idiom test_spl_log.cpp / test_spl_session_header.cpp already
// use: a directory that removes itself so a failing assertion cannot leave
// the next run reading a previous run's files.
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char* name)
        : path(std::filesystem::temp_directory_path() / "rta-test-spllogpipeline" / name) {
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

// DISTINGUISHABLE content, deliberately: blockIndex AND sumSquares both vary
// with `index`, so a mutation that reorders, drops, or duplicates a block --
// not just one that corrupts a shared constant -- changes what this test
// reads back. `blockSamples` is fixed (every block in one session shares it,
// same as a real SplMeter session) but sumSquares is a strictly increasing,
// index-derived function, so two blocks are never bit-identical.
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

// Every `<basePath>.genN.segM.csv` file this session could have written,
// sorted lexicographically -- which is also GENERATION/SEGMENT order for any
// single-digit generation and segment count (this file never rotates past
// segment 9), so concatenating their contents in this order reconstructs the
// log exactly as SplLogWriter wrote it. Scanning the directory rather than
// predicting the exact filename keeps this test decoupled from
// SplLogWriter's own naming convention, which is that class's business, not
// this pipeline's.
std::vector<std::filesystem::path> channelSegmentFiles(const std::filesystem::path& dir,
                                                         const std::string& basename) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind(basename + ".", 0) == 0 && entry.path().extension() == ".csv") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

LogReadResult readAllSegments(const std::filesystem::path& dir, const std::string& basename) {
    LogReadResult combined;
    for (const auto& file : channelSegmentFiles(dir, basename)) {
        const auto part = readLog(readWholeFile(file));
        combined.blocks.insert(combined.blocks.end(), part.blocks.begin(), part.blocks.end());
        combined.bytesDiscarded += part.bytesDiscarded;
    }
    return combined;
}

}  // namespace

// --- round-trip: N blocks, one channel, a session header, nothing discarded

TEST_CASE("SplLogPipeline round-trips pushed blocks bitwise and drains "
         "everything pushed before disable() returns",
         "[spl_log_pipeline]") {
    TempDir dir("roundtrip");

    SplLogEnableParams params;
    params.config = SplConfig{};
    params.channels = { channelSpec(dir.path, 2) };  // a non-zero channel on purpose
    params.sessionHeaderPath = (dir.path / "session.header.txt").string();
    params.startedAtUnixMs = 1'700'000'000'000ull;
    params.queueCapacityBlocks = 256;  // generous: 50 pushed, never full

    SplLogPipeline pipeline;
    pipeline.enable(params);

    // Pushed, then disable() called with NO sleep in between: the writer
    // thread's very first drainOnce() (called right after enable() starts
    // it) finds an empty ring and sleeps kIdleSleep (5 ms) -- five
    // milliseconds is generous headroom over how long 50 in-memory
    // pushBlock() calls plus this function call take (microseconds, no
    // syscalls), so the writer is almost certainly still asleep when
    // disable() flips `running_` to false below. That makes the blocks
    // reach disk ONLY through writerLoop()'s guaranteed POST-LOOP
    // drainOnce() -- exactly the "shutdown drains then joins" acceptance,
    // and a mutation that deletes that final call would leave them unwritten
    // here rather than being rescued by a lucky while-loop iteration.
    constexpr std::uint64_t kBlocks = 50;
    for (std::uint64_t i = 0; i < kBlocks; ++i) {
        pipeline.pushBlock(2, distinguishableBlock(i));
    }
    pipeline.disable();

    CHECK(pipeline.droppedBlocks(2) == 0);  // capacity 256 >> 50 pushed: never full

    const auto result = readAllSegments(dir.path, "ch2");
    CHECK(result.bytesDiscarded == 0);
    REQUIRE(result.blocks.size() == kBlocks);
    for (std::uint64_t i = 0; i < kBlocks; ++i) {
        INFO("block " << i);
        const Block expected = distinguishableBlock(i);
        CHECK(result.blocks[i].blockIndex == expected.blockIndex);
        CHECK(result.blocks[i].sumSquares == expected.sumSquares);  // BITWISE
    }

    // The session header names this channel's file and is itself present --
    // record §10's "one file per logged channel plus one session header",
    // wired end-to-end rather than asserted only at SplSessionHeader.h's own
    // unit level (test_spl_session_header.cpp already covers that level).
    REQUIRE(std::filesystem::exists(params.sessionHeaderPath));
    const std::string header = readWholeFile(params.sessionHeaderPath);
    CHECK(header.find("channelCount=1") != std::string::npos);
    CHECK(header.find("channelFile0=") != std::string::npos);
}

// --- rotation happens THROUGH the pipeline, not only inside SplLogWriter ---

TEST_CASE("SplLogPipeline rotates segments at config.segmentBlocks", "[spl_log_pipeline]") {
    TempDir dir("rotation");

    SplLogEnableParams params;
    params.config = SplConfig{};  // the REAL default (3600), not shrunk to dodge it --
                                  // test_spl_log.cpp's own C5 case at the SplLogWriter
                                  // level already establishes this exact number; what
                                  // is new here is proving the PIPELINE threads
                                  // config.segmentBlocks through to the writer it owns
    params.channels = { channelSpec(dir.path, 0) };
    params.queueCapacityBlocks = 8192;  // > 3601 pushed: no drop-related interference

    SplLogPipeline pipeline;
    pipeline.enable(params);
    const std::uint64_t kBlocks = params.config.segmentBlocks + 1;  // one past the first segment
    for (std::uint64_t i = 0; i < kBlocks; ++i) pipeline.pushBlock(0, distinguishableBlock(i));
    pipeline.disable();

    const auto files = channelSegmentFiles(dir.path, "ch0");
    CHECK(files.size() == 2);  // segmentBlocks in the first, 1 in the second

    const auto result = readAllSegments(dir.path, "ch0");
    CHECK(result.bytesDiscarded == 0);
    REQUIRE(result.blocks.size() == kBlocks);
    for (std::uint64_t i = 0; i < kBlocks; ++i) CHECK(result.blocks[i].blockIndex == i);
}

// The allocation-free producer case moved to test_spl_log_pipeline_alloc.cpp
// (round 4, LOW finding 3): this file was at 428 lines, over the project's
// 400-line hard cap; that test's own subject (AllocationProbe's per-thread
// attribution, round 4 finding 2) is a different concern from the round-trip
// / rotation / overflow / wiring cases here.

// --- a full queue is counted as dropped, and the producer never blocks ----

TEST_CASE("a full queue counts the block as dropped, never blocks the producer, "
         "and preserves the order of what does get written",
         "[spl_log_pipeline]") {
    TempDir dir("overflow");

    SplLogEnableParams params;
    params.config = SplConfig{};
    params.channels = { channelSpec(dir.path, 0) };
    params.queueCapacityBlocks = 8;  // tiny on purpose: the point of this test is overflow

    SplLogPipeline pipeline;
    pipeline.enable(params);

    // 20,000 pushes, back to back, no I/O of their own (pushBlock is a few
    // atomic ops on a fixed array -- see SplLogPipeline.h's own header
    // comment). If pushBlock ever WAITS for room instead of counting a full
    // queue as dropped, it waits for the CONSUMER's own disk write --
    // SplLogWriter::write() calls stream_.flush(), a real OS flush syscall,
    // which costs at minimum low tens of microseconds and routinely far more
    // under a journaled filesystem or a loaded CI runner. 20,000 such waits
    // would take seconds; 20,000 real pushes take low milliseconds. Two
    // seconds is generous headroom over the non-blocking case and would
    // still be a severe undercount of what a blocking mutation costs.
    constexpr std::uint64_t kPushed = 20000;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < kPushed; ++i) {
        pipeline.pushBlock(0, distinguishableBlock(i));
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(elapsed < std::chrono::seconds(2));

    pipeline.disable();

    const std::uint64_t dropped = pipeline.droppedBlocks(0);
    const auto result = readAllSegments(dir.path, "ch0");
    CHECK(result.bytesDiscarded == 0);

    // The core accounting invariant, independent of exactly WHEN the writer
    // thread happened to run: every pushed block is either written or
    // counted dropped, never both and never neither.
    CHECK(result.blocks.size() + dropped == kPushed);
    // The tiny 8-block capacity against a 20,000-block burst makes at least
    // one drop overwhelmingly likely (see the timing argument above: the
    // consumer cannot plausibly keep pace), and a run where it did not would
    // mean this test exercised no overflow at all.
    CHECK(dropped > 0);
    CHECK(result.blocks.size() < kPushed);

    // Order preserved among whatever DID make it through, and every value
    // that did is one of the pushed blocks' own distinguishable content --
    // catches a reordering or a silently-substituted block, not only a
    // count mismatch.
    for (std::size_t i = 0; i + 1 < result.blocks.size(); ++i) {
        CHECK(result.blocks[i].blockIndex < result.blocks[i + 1].blockIndex);
    }
    for (const auto& block : result.blocks) {
        const Block expected = distinguishableBlock(block.blockIndex);
        CHECK(block.sumSquares == expected.sumSquares);  // BITWISE
    }
}

// --- a channel with no sink is a silent, harmless no-op --------------------

TEST_CASE("pushBlock on a channel nothing was enabled for does nothing and does not crash",
         "[spl_log_pipeline]") {
    SplLogPipeline pipeline;  // never enabled at all
    pipeline.pushBlock(0, distinguishableBlock(0));
    CHECK(pipeline.droppedBlocks(0) == 0);

    TempDir dir("partial");
    SplLogEnableParams params;
    params.config = SplConfig{};
    params.channels = { channelSpec(dir.path, 0) };  // only channel 0
    pipeline.enable(params);
    pipeline.pushBlock(5, distinguishableBlock(0));  // channel 5 has no sink
    CHECK(pipeline.droppedBlocks(5) == 0);
    pipeline.disable();
}
