// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a task W2-E2a. Split out of test_spl_log_wiring.cpp (LOW follow-up
// batch, item 9 -- that file was at 455 lines, over the 400-line hard cap)
// along its own natural seam: this file's ONE subject is disableSplLogging()
// actually draining and joining the writer thread, not returning while a
// background writer is still catching up on its own. See
// test_spl_log_wiring.cpp's own header comment for the wiring this file
// shares its subject with.
//
// Own copies of the small TempDir/fastConfig/splConfig/pushBlocks/
// waitForSplBlocks/waitForSplLoggingOff/readWholeFile/csvFilesIn fixtures
// test_spl_log_wiring.cpp's anonymous namespace also declares -- an
// anonymous namespace cannot be shared across TUs, the same trade-off
// test_spl_log_pipeline.cpp's own multi-file split already makes.
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
/// other SPL request (AnalysisThread.h's own class comment) -- so the
/// caller keeps feeding hops until `splBlockCount` has been reset by the
/// disable actually landing, the same "wait for the request to land" idiom
/// `waitForSplBlocks` above uses for the opposite direction.
bool waitForSplLoggingOff(AnalysisThread& thread, CaptureBus& bus, int channel, int timeoutMs) {
    const auto deadline =
        juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        if (thread.splBlockCount(channel) == 0) return true;
        pushBlocks(bus, 1, 16, 1);
        juce::Thread::sleep(5);
    }
    return thread.splBlockCount(channel) == 0;
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

// --- LOW follow-up batch, item 9: disableSplLogging() actually drains and
// joins the writer thread, not a no-op that "stays green" only because the
// background writer catches up on its own before the test reads the files --

TEST_CASE("disableSplLogging() leaves every pushed block on disk before it returns",
          "[spl_log_wiring]") {
    // MUTANT: making `AnalysisThreadSpl.cpp`'s `applyPendingSplRequest()`
    // treat `splLogPipeline_.disable()` as a no-op on the disable path stayed
    // GREEN against test_spl_log_wiring.cpp's own FIRST test (16 blocks):
    // that test's own `pushBlocks`/`waitForSplBlocks` loop, plus the polling
    // in `waitForSplLoggingOff` below, already gives a background writer
    // thread (kIdleSleep = 5 ms between drain passes, SplLogPipeline.cpp)
    // many milliseconds to catch up on its own -- ample time to flush 16
    // tiny CSV rows even if nothing ever joins it. `disable()`'s own real
    // job -- "shutdown drains then joins" (SplLogPipeline.h's own class
    // comment) -- is what a caller is relying on to make the file complete
    // and closeable the INSTANT `disableSplLogging()`'s request has landed,
    // not "usually complete a few milliseconds later".
    //
    // The adversarial fixture: 500 blocks (2000 tiny hops) pushed back to
    // back with NO pause before `disableSplLogging()`, the same "overwhelming
    // probability, not exact timing" shape `test_spl_log_pipeline.cpp`'s own
    // "a full queue" case already uses in this codebase -- SplLogWriter::
    // write() calls `stream_.flush()`, a real OS syscall costing at minimum
    // low tens of microseconds (that test's own measured argument), so
    // flushing 500 rows costs on the order of several to tens of
    // milliseconds of REAL disk I/O -- while the in-memory push loop below
    // that feeds them costs microseconds. A background writer with no join
    // forcing it to finish is, at the moment this test reads the files
    // immediately after `disableSplLogging()` lands, overwhelmingly likely to
    // still be mid-flush.
    TempDir dir("disable-drains-and-joins");
    CaptureBus bus(1 << 16);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    bus.prepare(48000.0, 1);
    bus.setActive(true);

    AnalysisThread thread(bus, fastConfig());
    const std::array<int, 1> channels{ 0 };
    thread.enableSplLogging(splConfig(48000.0), channels, dir.path.string());

    constexpr int kBlocks = 500;
    constexpr int kHopsPerBlock = 4;  // splConfig(): 64 samples/block, 16/hop
    pushBlocks(bus, 1, 16, kBlocks * kHopsPerBlock);
    REQUIRE(waitForSplBlocks(thread, 0, kBlocks, 5000));

    thread.disableSplLogging();
    REQUIRE(waitForSplLoggingOff(thread, bus, 0, 5000));

    // Read IMMEDIATELY -- no sleep, no extra poll beyond confirming the
    // disable request landed. Under the mutant this reads a short file
    // (blocks still queued in memory, or mid-flush and `bytesDiscarded`
    // nonzero from a partially written last line); under the real fix,
    // `disable()` already blocked until every one of the 500 rows was
    // written and the file closed, so this is unconditionally complete.
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
