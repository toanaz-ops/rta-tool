// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a task W2-E2a's own W0-B0 acceptance: the producer (pushBlock())
// never allocates. Split out of test_spl_log_pipeline.cpp (round 4, PR #31,
// LOW finding 3 -- that file was at 428 lines, over the 400-line hard cap).
// Pure relocation in this commit -- no behaviour change; round 4 finding 2
// (AllocationProbe's own per-thread attribution) simplifies the body of this
// test in the very next commit.
#include "export/SplLogPipeline.h"

#include "AllocationProbe.h"

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

// Same TempDir idiom every SplLog* test file uses: a directory that removes
// itself so a failing assertion cannot leave the next run reading a
// previous run's files.
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char* name)
        : path(std::filesystem::temp_directory_path() / "rta-test-spllogpipeline-alloc" / name) {
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

TEST_CASE("pushBlock is allocation-free after enable(), pushing 10x queue capacity",
         "[spl_log_pipeline]") {
    TempDir dir("alloc");

    SplLogEnableParams params;
    params.config = SplConfig{};
    params.channels = { channelSpec(dir.path, 0) };
    params.queueCapacityBlocks = 64;

    SplLogPipeline pipeline;

    // Station-4 fix round (PR #31, finding 4) moved SplLogWriter's own
    // construction -- opening the file, writing the header, an internal
    // stream buffer allocation -- off enable() and onto the writer thread's
    // own first act (setupWriters()). AllocationProbe is a GLOBAL, process-
    // wide counter (its whole point: catch an allocation from ANY thread,
    // memory/an-allocation-the-optimiser-removed-reads-as-zero-bytes.md), so
    // if that setup is still running when the probe scope below starts, its
    // allocations get charged to pushBlock() even though they are the
    // writer thread's own one-time setup, not the producer. Wait for setup
    // to finish (this same injection seam test_spl_log_pipeline_writefailed
    // .cpp's own "opens the log file on the writer thread" case already
    // uses) BEFORE opening the probe scope -- caught intermittently on
    // ubuntu-latest CI (32 bytes over 640 pushBlock calls) once finding 4
    // made the timing possible; it did not reproduce locally on Windows,
    // consistent with a race whose odds differ by OS scheduler.
    std::atomic<bool> writerReady{ false };
    pipeline.setWriterFactoryForTest(
        [&](const std::string& basePath, const SplConfig& config,
            const rta::splexport::SplLogHeaderInfo& info, std::uint64_t segmentBlocks) {
            auto writer = std::make_unique<rta::splexport::SplLogWriter>(basePath, config, info,
                                                                         segmentBlocks);
            writerReady.store(true, std::memory_order_release);
            return writer;
        });
    pipeline.enable(params);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (!writerReady.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(writerReady.load(std::memory_order_acquire));

    std::size_t bytes = 0;
    {
        // Non-elidable (memory/an-allocation-the-optimiser-removed-reads-as-
        // zero-bytes.md): every pushed block's effect ESCAPES this scope
        // through `pipeline`'s own queue/dropped-counter state, which the
        // assertions below read back, so the optimiser cannot prove the
        // calls are dead and elide them.
        const rta::test::AllocationProbe probe;
        for (std::uint64_t i = 0; i < params.queueCapacityBlocks * 10; ++i) {
            pipeline.pushBlock(0, distinguishableBlock(i));
        }
        bytes = probe.bytes();
    }
    INFO("bytes allocated by " << params.queueCapacityBlocks * 10 << " pushBlock calls = " << bytes);
    CHECK(bytes == 0);

    pipeline.disable();
}
