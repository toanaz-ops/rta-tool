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
//
// test_spl_log_wiring_disable.cpp: LOW follow-up batch, item 9 -- split out
// of this file (455 lines, over the 400-line hard cap) along its own natural
// seam, the disableSplLogging()-actually-drains-and-joins case.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "export/SplLog.h"
#include "export/SplSessionHeader.h"
#include "measure/AnalysisThread.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numbers>
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

/// A REAL sine, phase-continuous across calls via `sampleIndex` (the
/// caller's own running counter) -- single channel only, one hop per call to
/// `bus.pushFromCallback`. Needed for the A-vs-Z weighting test below: a
/// constant value (what `pushBlocks` above feeds) is degenerate at low
/// frequency and cannot distinguish a filter's passband from its stopband.
void pushSineTone(CaptureBus& bus, double frequencyHz, double sampleRate, float amplitude,
                  int hopSize, int hops, std::uint64_t& sampleIndex) {
    constexpr double kTwoPi = 2.0 * std::numbers::pi;
    std::vector<float> hop(static_cast<std::size_t>(hopSize));
    for (int h = 0; h < hops; ++h) {
        for (int i = 0; i < hopSize; ++i) {
            const double t = static_cast<double>(sampleIndex) / sampleRate;
            hop[static_cast<std::size_t>(i)] =
                static_cast<float>(static_cast<double>(amplitude) * std::sin(kTwoPi * frequencyHz * t));
            ++sampleIndex;
        }
        const float* ptr = hop.data();
        bus.pushFromCallback(&ptr, 1, hopSize);
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

// --- mutation-4 closure (station-4 verification pass): the log records the
// A chain by CONTENT, not only by the header's own label --------------------

TEST_CASE("the logged chain is A-weighted, not Z, at a frequency where the "
         "two curves disagree by 39 dB",
         "[spl_log_wiring]") {
    // IEC 61672-1 Table 3, exact third-octave frequency f = 1000*10^(0.1n)
    // at n = -15 (core/tests/test_weighting.cpp's own citation and table):
    // A(f) = -39.4 dB. Z is unity gain BY DEFINITION (Weighting::analyticDb
    // returns exactly 0.0 for WeightingType::Z at every frequency) -- it is
    // not a filter at all, so there is no settling question on that side.
    //
    // A default SplConfig (no metrics) auto-creates exactly the Z chain
    // (first, since nothing names a weighting) and the A chain (always
    // second -- SplSession::start's "an A-weighted chain always exists"
    // rule), and record §10's own worked log example is stamped
    // "weighting=A". If AnalysisThreadSpl.cpp's feedSpl() ever pushed the Z
    // chain's blocks into the log pipeline while the header still says A
    // (the exact shape of a chain-selection mutation -- the header is a
    // separate, hardcoded literal from the block-selection call), the level
    // on disk would sit ~39 dB HIGHER than this test's closed-form A
    // expectation: a wrong number under the right label.
    constexpr double kFrequencyHz = 1000.0 * 0.03162277660168379;  // 1000*10^-1.5, exact
    constexpr double kADb = -39.4;
    constexpr float kAmplitude = 0.5f;
    constexpr double kSampleRate = 48000.0;

    TempDir dir("chain-a");
    // Large enough to hold the WHOLE burst below (192000 samples) with
    // margin, so this test's outcome does not depend on how fast the
    // analysis thread happens to drain relative to the test thread's push
    // loop -- nothing can be dropped from the ring regardless of scheduling.
    CaptureBus bus(1 << 18);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    bus.prepare(kSampleRate, 1);
    bus.setActive(true);

    AnalysisThread thread(bus, fastConfig());
    SplConfig config;
    // 24000 samples/block: long enough that a weighting filter (a cascaded
    // 2nd-order IIR with its lowest corner near 20 Hz, time constant on the
    // order of 1/(2*pi*20 Hz) ~= 8 ms) has settled by many hundreds of time
    // constants before the LAST of eight such blocks, which is the only one
    // this test reads.
    config.blockSeconds = 0.5;
    const std::array<int, 1> channels{ 0 };
    thread.enableSplLogging(config, channels, dir.path.string());

    constexpr int kHopSize = 16;
    constexpr int kBlocksToFeed = 8;
    const int hopsNeeded =
        static_cast<int>(config.blockSeconds * kSampleRate / kHopSize) * kBlocksToFeed;
    std::uint64_t sampleIndex = 0;
    pushSineTone(bus, kFrequencyHz, kSampleRate, kAmplitude, kHopSize, hopsNeeded, sampleIndex);
    REQUIRE(waitForSplBlocks(thread, 0, kBlocksToFeed, 5000));

    thread.disableSplLogging();
    REQUIRE(waitForSplLoggingOff(thread, bus, 0, 3000));

    const auto files = csvFilesIn(dir.path);
    REQUIRE_FALSE(files.empty());
    const std::string fileText = readWholeFile(files.front());
    const auto result = rta::splexport::readLog(fileText);
    REQUIRE(result.bytesDiscarded == 0);
    REQUIRE(result.blocks.size() == static_cast<std::size_t>(kBlocksToFeed));

    const auto& lastBlock = result.blocks.back();  // fully settled by now
    REQUIRE(lastBlock.blockSamples > 0);
    const double measuredDb =
        10.0 * std::log10(lastBlock.sumSquares / static_cast<double>(lastBlock.blockSamples));

    // Mean-square dBFS convention (SplMeter::blockLevelDb's own comment):
    // 20*log10(amplitude) - 3.0102999566398120 for a full sine, then the
    // weighting's own steady-state gain on top.
    const double expectedUnweightedDb =
        20.0 * std::log10(static_cast<double>(kAmplitude)) - 3.0102999566398120;
    const double expectedADb = expectedUnweightedDb + kADb;

    INFO("measured = " << measuredDb << " dB, expected A = " << expectedADb
                        << " dB, expected Z (unweighted) = " << expectedUnweightedDb << " dB");
    // TOLERANCE, DERIVED (LOW follow-up batch, item 8) -- the untyped 1 dB
    // hid a mutant-4-sized regression instead of catching one. Three
    // independent components, summed:
    //   (a) Table-3 rounding, ~0.04 dB -- kADb above is IEC 61672-1 Table 3's
    //       PUBLISHED figure at this frequency, itself rounded to 0.1 dB; the
    //       digital cascade tracks the ANALYTIC curve (test_weighting.cpp's
    //       own "digital filter tracks the analytic curve" case: 0.01 dB),
    //       not the published rounding, so up to half that rounding step
    //       (~0.04 dB, this frequency's own analytic-vs-Table-3 residual) is
    //       real and belongs in the bound, not folded into slack.
    //   (b) the finite-window mean-square term, ~0.022 dB -- the last block
    //       spans kFrequencyHz * blockSeconds = 15.81 CYCLES, not a whole
    //       number, so `sumSquares / blockSamples` differs from the ideal
    //       amplitude^2/2 by a term that shrinks with cycle count (settled by
    //       many time constants, per this test's own comment above, so this
    //       is the dominant remaining error, not filter transient).
    //   (c) the digital-vs-analytic filter residual, 0.01 dB --
    //       test_weighting.cpp's own accepted bound for the SAME comparison.
    // Sum ~0.072 dB, rounded up to ~0.1 dB; the measured residual here is
    // 0.077 dB (re-measured after the LOW follow-up batch's own follow-up --
    // an earlier draft of this comment cited 0.037 dB, which was stale),
    // matching the derived sum to within 0.005 dB and comfortably inside the
    // shipped bound. The shipped bound is ~0.2 dB -- roughly 2x the derived
    // sum and 2.6x the measured residual -- so it stays tight enough to catch
    // a real regression (a wrong chain reads ~39 dB away, not a fraction of a
    // dB) while not chasing the derivation's own last digit.
    CHECK_THAT(measuredDb, Catch::Matchers::WithinAbs(expectedADb, 0.2));
    // The hard refutation, independent of the tolerance above: a Z-labelled-
    // as-A block reads within a fraction of a dB of expectedUnweightedDb,
    // 39.4 dB higher than expectedADb. Half that gap (19.7 dB) is still far
    // more margin than any settling residual or filter tolerance could close.
    CHECK(measuredDb < expectedUnweightedDb - (std::abs(kADb) / 2.0));

    // The label itself must say A too -- catches the OTHER half of a
    // mismatch: a mutation that changed which chain's BLOCKS get pushed
    // without touching applyPendingSplRequest's separate, hardcoded
    // `spec.info.weighting = WeightingType::A` would still print
    // "weighting=A" over the wrong chain's own numbers. Both halves have to
    // agree for this test to mean what it says.
    CHECK(fileText.find("weighting=A") != std::string::npos);
}

// --- station-4 fix round (PR #31, round 3, LOW finding 4): the
// splLogWriteFailed_ mirror is reset on disable, same as splLogDroppedBlocks_

TEST_CASE("splLogWriteFailed() clears after disableSplLogging(), not just "
         "splLogDroppedBlocks()",
         "[spl_log_wiring]") {
    // A directory that was never created makes every segment open underneath
    // it fail (SplLogWriter's own "does-not-exist" idiom, test_spl_log.cpp
    // and test_spl_log_pipeline.cpp both use it) -- the simplest way to force
    // splLogWriteFailed_ to true without touching disk permissions.
    const auto missingDir = std::filesystem::temp_directory_path() / "rta-test-spllogwiring" /
                            "does-not-exist-6a2f9";
    std::filesystem::remove_all(missingDir);

    CaptureBus bus(4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    bus.prepare(48000.0, 1);
    bus.setActive(true);

    AnalysisThread thread(bus, fastConfig());
    const std::array<int, 1> channels{ 0 };
    thread.enableSplLogging(splConfig(48000.0), channels, missingDir.string());

    pushBlocks(bus, 1, 16, 64);
    REQUIRE(waitForSplBlocks(thread, 0, 16, 3000));  // blocks close regardless

    // The mirror is only refreshed FROM feedSpl(), which only runs when
    // there is a new hop to drain -- so, unlike waitForSplBlocks above (a
    // one-shot condition, once true forever), this poll must keep feeding
    // hops the whole time, the same "keep pushing while waiting" shape
    // waitForSplLoggingOff already uses below. Without this, the check
    // races the writer thread's own setupWriters() (a brand new
    // std::thread's first OS time-slice) rather than the FIX under test.
    const auto deadline = juce::Time::getMillisecondCounter() + 3000;
    while (juce::Time::getMillisecondCounter() < deadline && !thread.splLogWriteFailed(0)) {
        pushBlocks(bus, 1, 16, 1);
        juce::Thread::sleep(5);
    }
    REQUIRE(thread.splLogWriteFailed(0));

    thread.disableSplLogging();
    REQUIRE(waitForSplLoggingOff(thread, bus, 0, 3000));

    // THE FIX: without resetting splLogWriteFailed_ alongside
    // splLogDroppedBlocks_ in applyPendingSplRequest(), this stays true
    // forever -- a channel disabled after one bad session would show "LOG
    // WRITE FAILED" even once nothing is logging at all.
    CHECK_FALSE(thread.splLogWriteFailed(0));
}
