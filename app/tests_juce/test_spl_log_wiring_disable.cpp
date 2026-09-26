// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a task W2-E2a. Split out of test_spl_log_wiring.cpp (LOW follow-up
// batch, item 9 -- that file was at 455 lines, over the 400-line hard cap)
// along its own natural seam: this file's ONE subject is disableSplLogging()
// actually calling through to SplLogPipeline::disable(), not a no-op that
// "stays green" only because a background writer happens to catch up on its
// own. This proves the CALL happens, deterministically -- it does not, and
// cannot without a slow-writer test hook this batch does not add, prove that
// the drain+join it starts has already FINISHED by the time this test reads
// the files (see the TEST_CASE's own comment for why a timing race cannot
// prove that here); SplLogPipeline::disable()'s own drain-then-join
// correctness is proven directly, at its own level, by
// test_spl_log_pipeline.cpp. See test_spl_log_wiring.cpp's own header
// comment for the wiring this file shares its subject with.
//
// Own copies of the small TempDir/fastConfig/splConfig/pushBlocks/
// readWholeFile/csvFilesIn fixtures test_spl_log_wiring.cpp's anonymous
// namespace also declares -- an anonymous namespace cannot be shared across
// TUs, the same trade-off test_spl_log_pipeline.cpp's own multi-file split
// already makes.
#include <catch2/catch_test_macros.hpp>

#include "export/SplLog.h"
#include "measure/AnalysisThread.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using rta::measure::Analyser;
using rta::measure::AnalysisThread;
using rta::measure::SplConfig;
using rta::platform::CaptureBus;
using rta::platform::ChannelRole;

namespace {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char* name)
        : path(std::filesystem::temp_directory_path() / "rta-test-spllogwiring" / name) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

Analyser::Config fastConfig() {
    Analyser::Config config;
    config.fftSize = 64;
    config.hopSize = 16;
    config.sampleRate = 48000.0;
    config.transferFifoDepth = 4;
    config.mtwEnabled = false;  // the DRAIN/log wiring is under test, not the DSP
    return config;
}

/// Blocks short enough that a handful of 16-sample hops closes one --
/// test_spl_drain.cpp's own convention, reused here so this file needs no
/// new reasoning about the block clock.
SplConfig splConfig(double sampleRate) {
    SplConfig config;
    config.blockSeconds = 64.0 / sampleRate;  // 64 samples = 4 hops of 16
    return config;
}

void pushBlocks(CaptureBus& bus, int channelsInCallback, int samplesPerBlock, int blocks) {
    std::vector<float> tone(static_cast<std::size_t>(samplesPerBlock), 0.1f);
    std::vector<const float*> ptrs(static_cast<std::size_t>(channelsInCallback), tone.data());
    for (int b = 0; b < blocks; ++b) {
        bus.pushFromCallback(ptrs.data(), channelsInCallback, samplesPerBlock);
    }
}

bool waitForSplBlocks(const AnalysisThread& thread, int channel, std::uint64_t target,
                      int timeoutMs) {
    const auto deadline =
        juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        if (thread.splBlockCount(channel) >= target) return true;
        juce::Thread::sleep(5);
    }
    return thread.splBlockCount(channel) >= target;
}

/// `disableSplLogging()` is picked up on the NEXT `drain()`, same as every
/// other SPL request (AnalysisThread.h's own class comment), and `drain()`
/// runs on the analysis thread's own `kPollMs` cadence regardless of whether
/// new audio keeps arriving (`AnalysisThread::runBody()`'s own loop) -- so,
/// unlike waiting for METERING to reach a target block count, waiting for
/// this to land needs no continued hop-feeding at all.
bool waitForSplLoggingOff(const AnalysisThread& thread, int timeoutMs) {
    const auto deadline =
        juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        if (!thread.isSplLoggingEnabled()) return true;
        juce::Thread::sleep(5);
    }
    return !thread.isSplLoggingEnabled();
}

std::string readWholeFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::filesystem::path> csvFilesIn(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(dir)) return files;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() == ".csv") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

}  // namespace

// --- LOW follow-up batch, item 9: disableSplLogging() actually calls
// SplLogPipeline::disable() (which itself drains then joins its writer
// thread -- SplLogPipeline.h's own class comment, exercised directly by
// test_spl_log_pipeline.cpp), rather than a no-op that "stays green" ---

TEST_CASE("disableSplLogging() actually calls through to SplLogPipeline::disable()",
          "[spl_log_wiring]") {
    // MUTANT: making `AnalysisThreadSpl.cpp`'s `applyPendingSplRequest()`
    // treat `splState_.logPipeline.disable()` as a no-op on the disable path
    // stayed GREEN against every timing-based version of this test tried
    // while writing it -- a burst-then-read-the-files race, even scaled up
    // to 8 channels x 200 blocks (1600 rows), never caught it. Measured
    // directly: this project's analysis thread meters incoming audio no
    // faster than roughly one hop per `kPollMs` (10 ms) poll tick, so an
    // 800-hop, 8-channel burst took ~1.2 s of REAL time just to finish
    // metering -- and `SplLogPipeline::pushBlock()` is called incrementally
    // as each block closes DURING that whole window, not all at once at the
    // end. A background writer thread, whose own per-row cost
    // (`stream_.flush()`, tens of microseconds) is two to three orders of
    // magnitude smaller than that metering cadence, never meaningfully
    // falls behind -- so by the time any burst this test could practically
    // push finishes metering, the files are already complete regardless of
    // whether `disable()` was ever called at all. No burst size fixes this;
    // the bottleneck this test would need to race is upstream of the
    // pipeline entirely.
    //
    // The reliable, race-free signal instead: `SplLogPipeline::disable()`
    // clears its own `running_` flag as the very FIRST thing it does, before
    // the join that follows (SplLogPipeline.cpp's own comment on
    // `disable()`). `AnalysisThread::isSplLoggingEnabled()` mirrors that flag
    // directly. Under the mutant, `disable()` is never called, so `running_`
    // never clears and this stays `true` forever; under the real fix, it
    // reliably flips to `false` once the pending disable request lands, with
    // no dependency on writer-thread timing at all.
    TempDir dir("disable-calls-through");
    CaptureBus bus(1 << 16);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    bus.prepare(48000.0, 1);
    bus.setActive(true);

    AnalysisThread thread(bus, fastConfig());
    const std::array<int, 1> channels{ 0 };
    thread.enableSplLogging(splConfig(48000.0), channels, dir.path.string());

    constexpr int kBlocks = 20;
    constexpr int kHopsPerBlock = 4;  // splConfig(): 64 samples/block, 16/hop
    pushBlocks(bus, 1, 16, kBlocks * kHopsPerBlock);
    // Implies the enable request already landed -- metering could not
    // otherwise have produced any blocks on this channel.
    REQUIRE(waitForSplBlocks(thread, 0, kBlocks, 5000));
    REQUIRE(thread.isSplLoggingEnabled());

    thread.disableSplLogging();
    REQUIRE(waitForSplLoggingOff(thread, 2000));

    // Secondary check, still real regression protection even though it is
    // not what catches the no-op mutant above (see this test's own header
    // comment for why a timing race cannot catch it): once the pipeline
    // reports itself disabled, the files it wrote should be complete, not
    // merely non-empty.
    const auto files = csvFilesIn(dir.path);
    REQUIRE_FALSE(files.empty());
    std::size_t totalBlocks = 0;
    for (const auto& file : files) {
        const auto result = rta::splexport::readLog(readWholeFile(file));
        CHECK(result.bytesDiscarded == 0);
        totalBlocks += result.blocks.size();
    }
    CHECK(totalBlocks == static_cast<std::size_t>(kBlocks));
}
