// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a task W2-E2a (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md's
// amendment "W2-E -- the wiring nobody was assigned"; record docs/dsp/
// 2026-09-16-spl-pro-l6a.md §10). What only a live AnalysisThread can prove:
// that `enableSplLogging`'s `logDirectory` argument actually reaches disk,
// that `Snapshot::spl` becomes present once a session is running (the plan's
// own ON acceptance), and that `disableSplLogging()` leaves a file a plain
// reader can open immediately -- no writer thread still appending in the
// background after the call has landed. The queue/writer mechanics
// themselves are proven OFF in app/tests/test_spl_log_pipeline.cpp; this
// file is only the wiring between AnalysisThread and that pipeline.
#include <catch2/catch_test_macros.hpp>

#include "export/SplLog.h"
#include "export/SplSessionHeader.h"
#include "measure/AnalysisThread.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
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

TEST_CASE("enableSplLogging's logDirectory writes a real, readable log and "
         "publishes Snapshot::spl once a block closes",
         "[spl_log_wiring]") {
    TempDir dir("enable");
    CaptureBus bus(4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    bus.prepare(48000.0, 1);
    bus.setActive(true);

    AnalysisThread thread(bus, fastConfig());
    const std::array<int, 1> channels{ 0 };
    thread.enableSplLogging(splConfig(48000.0), channels, dir.path.string());

    pushBlocks(bus, 1, 16, 64);  // 64 hops of 16 = 16 blocks of 64 samples
    REQUIRE(waitForSplBlocks(thread, 0, 16, 3000));

    // Snapshot::spl: absent until logging is on AND a block has closed
    // (W0-C's own rule) -- both are now true.
    const auto snapshot = thread.latest();
    REQUIRE(snapshot != nullptr);
    REQUIRE(snapshot->spl.has_value());
    // Default queue capacity (256) is nowhere near 16 blocks: nothing was
    // dropped from the log.
    CHECK(snapshot->spl->logDroppedBlocks == 0);

    thread.disableSplLogging();
    REQUIRE(waitForSplLoggingOff(thread, bus, 0, 3000));

    // The file is fully written and closeable RIGHT NOW: disableSplLogging()
    // having landed means SplLogPipeline::disable() already joined its
    // writer thread (AnalysisThreadSpl.cpp's applyPendingSplRequest calls it
    // synchronously, on the analysis thread, before returning) -- there is
    // no background writer left that could still be mid-append.
    const auto files = csvFilesIn(dir.path);
    REQUIRE_FALSE(files.empty());
    std::size_t totalBlocks = 0;
    for (const auto& file : files) {
        const auto result = rta::splexport::readLog(readWholeFile(file));
        CHECK(result.bytesDiscarded == 0);
        totalBlocks += result.blocks.size();
    }
    CHECK(totalBlocks == 16);

    const auto headerPath = dir.path / "session.header.txt";
    REQUIRE(std::filesystem::exists(headerPath));
    const std::string sessionHeaderText = readWholeFile(headerPath);
    CHECK(sessionHeaderText.find("channelFile0=") != std::string::npos);
    // Mutation gap closed (station-4 mutation pass, PR round): the header's
    // blockSamples must come from blockSamplesFor(config.blockSeconds,
    // sampleRate), not any other figure -- fastConfig()/splConfig() above
    // give 64 samples/block exactly (64/48000 s @ 48000 Hz), and no other
    // assertion in this file or in test_spl_log_pipeline.cpp reads this key.
    CHECK(sessionHeaderText.find("blockSamples=64") != std::string::npos);
}

TEST_CASE("enableSplLogging with no logDirectory keeps Snapshot::spl live "
         "but writes nothing to disk",
         "[spl_log_wiring]") {
    CaptureBus bus(4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    bus.prepare(48000.0, 1);
    bus.setActive(true);

    AnalysisThread thread(bus, fastConfig());
    const std::array<int, 1> channels{ 0 };
    thread.enableSplLogging(splConfig(48000.0), channels);  // no logDirectory: state only

    pushBlocks(bus, 1, 16, 64);
    REQUIRE(waitForSplBlocks(thread, 0, 16, 3000));

    const auto snapshot = thread.latest();
    REQUIRE(snapshot != nullptr);
    CHECK(snapshot->spl.has_value());  // W2-E1's own state still runs unchanged
    CHECK(snapshot->spl->logDroppedBlocks == 0);  // nothing was ever asked to log

    thread.disableSplLogging();
}
