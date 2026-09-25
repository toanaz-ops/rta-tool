// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a task W2-E2a's own W0-B0 acceptance: the producer (pushBlock())
// never allocates. Split out of test_spl_log_pipeline.cpp (round 4, PR #31,
// LOW finding 3 -- that file was at 428 lines, over the 400-line hard cap)
// because this ONE case's subject changed in this same commit: it is no
// longer "does pushBlock itself allocate", it is "does AllocationProbe
// correctly attribute an allocation to the thread that armed it" (round 4
// finding 2).
//
// CI-discovered regression, round 3: finding 4 (this same PR) moved
// SplLogWriter's own construction -- opening the file, writing its header,
// an internal stream buffer allocation -- off enable() and onto the writer
// thread's own first act (setupWriters()). AllocationProbe was a GLOBAL,
// process-wide counter with no thread attribution at all, so if that setup
// was still running when this test's probe scope opened, its allocations
// landed inside the measured window even though they are the writer
// thread's own one-time setup, never a pushBlock() call. Caught on
// ubuntu-latest CI (32 bytes over 640 calls); did not reproduce on Windows,
// consistent with a race whose odds differ by OS scheduler.
//
// The previous commit's version of this test (moved here unchanged from
// test_spl_log_pipeline.cpp) patched this by waiting for setup to finish
// before opening the probe scope -- correct, but only NARROWS the race
// rather than closing it (a slower CI runner, a different allocation inside
// setupWriters() later, or ANY other background thread doing legitimate
// work during the window could still land in the shared, unattributed
// counter). The real fix is in AllocationProbe itself (AllocationProbe.cpp
// /.h, this same commit): the probe now only counts allocations on the
// THREAD THAT ARMED IT, so this test needs no waiting at all -- the writer
// thread's setup can run at any time, on any schedule, without ever
// touching what this scope measures.
#include "export/SplLogPipeline.h"

#include "AllocationProbe.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>

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
    pipeline.enable(params);

    // NO wait for the writer thread's own setup here (see this file's own
    // header comment): AllocationProbe's per-thread attribution means
    // whatever that thread allocates, whenever it runs, is never charged to
    // THIS scope, which only counts allocations on the thread that
    // constructed `probe` -- this one.
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
