// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L6a task W0-D (research §C4, SPL-R1): the SPL feed, against a REAL
// AnalysisThread over a REAL CaptureBus.
//
// What only a live thread can prove is the TAP POSITION. The gap arithmetic,
// the block clock and the window are all proven OFF in
// app/tests/test_spl_session.cpp; what is here is the three things the drain
// itself decides:
//
//   D1  the tap is on the SCRATCH, never a second read of the ring
//   D1b a route PAST kMaxTransferFunctions still logs
//   D2  an UNROUTED session logs with no reference at all
//   D3  the routed path's reference gate is REPORTED, with a length
//
// Feeds the bus through pushFromCallback, the same entry point the real audio
// callback and SyntheticInput both use -- test_routing_live.cpp's own reason.
#include <catch2/catch_test_macros.hpp>

#include "CodeLines.h"

#include "measure/AnalysisThread.h"

#include <juce_core/juce_core.h>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

using rta::measure::Analyser;
using rta::measure::AnalysisThread;
using rta::measure::SplConfig;
using rta::platform::CaptureBus;
using rta::platform::ChannelRole;

namespace {

Analyser::Config fastConfig() {
    Analyser::Config config;
    config.fftSize = 64;
    config.hopSize = 16;
    config.sampleRate = 48000.0;
    config.transferFifoDepth = 4;
    config.mtwEnabled = false;  // the DRAIN is under test, not the DSP
    return config;
}

/// Blocks short enough that a handful of 16-sample hops closes one: the
/// property under test is whether the feed is REACHED, and a 1 s block at
/// 48 kHz would need 3000 hops before saying so.
SplConfig splConfig(double sampleRate) {
    SplConfig config;
    config.blockSeconds = 64.0 / sampleRate;  // 64 samples = 4 hops
    config.metrics.push_back(rta::measure::SplMetricSpec{
        "L", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, 4});
    return config;
}

void pushBlocks(CaptureBus& bus, std::span<const int> toneChannels, int channelsInCallback,
                int samplesPerBlock, int blocks) {
    std::vector<float> silence(static_cast<std::size_t>(samplesPerBlock), 0.0f);
    std::vector<float> tone(static_cast<std::size_t>(samplesPerBlock), 0.1f);
    std::vector<const float*> ptrs(static_cast<std::size_t>(channelsInCallback), silence.data());
    for (int ch : toneChannels) {
        if (ch >= 0 && ch < channelsInCallback) {
            ptrs[static_cast<std::size_t>(ch)] = tone.data();
        }
    }
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

}  // namespace

// --- D1: the tap is on the scratch, never a second ring read -------------

TEST_CASE("D1 the SPL feed adds no peek and no discard to AnalysisThread", "[spl_drain]") {
    // rta::dsp::RingBuffer keeps ONE readIndex_ and drainPaired is its owner
    // under a routing plan (record §0 hazard 2). A second cursor is a
    // platform/ change and is explicitly not built here, so research §C4's
    // "the SPL meter must sit on the measurement channel's own drain" is
    // implemented as a tap on the scratch buffers that drain already filled
    // -- the shape locateBuffer_.feedHop already uses.
    //
    // codeText strips comments, so the several sentences ABOUT peek and
    // discard in this lane's own comments cannot match.
    const auto measureDir =
        std::filesystem::path(RTA_REPO_ROOT) / "app" / "src" / "measure";
    const auto count = [](const std::string& text, const std::string& needle) {
        int n = 0;
        std::size_t pos = 0;
        while ((pos = text.find(needle, pos)) != std::string::npos) {
            ++n;
            pos += needle.size();
        }
        return n;
    };

    const std::string drain = rta::test::codeText(measureDir / "AnalysisThread.cpp");
    // Three peeks and three discards, exactly as before this lane: the ref
    // peek and discard plus the per-route measurement peek and discard in
    // drainPaired, and the one peek/discard pair in drainRole.
    INFO("->peek( in AnalysisThread.cpp    = " << count(drain, "->peek("));
    INFO("->discard( in AnalysisThread.cpp = " << count(drain, "->discard("));
    CHECK(count(drain, "->peek(") == 3);
    CHECK(count(drain, "->discard(") == 3);

    // And the SPL half -- the split-off translation unit and the session --
    // touch a ring at all: zero.
    for (const char* file : {"AnalysisThreadSpl.cpp", "SplSession.cpp", "SplMeter.cpp"}) {
        const std::string code = rta::test::codeText(measureDir / file);
        INFO("file: " << file);
        CHECK(count(code, "->peek(") == 0);
        CHECK(count(code, "->discard(") == 0);
        CHECK(code.find("ringbuffer") == std::string::npos);
    }
}

// --- D1b: a route past the cap still logs --------------------------------

TEST_CASE("D1b a route past kMaxTransferFunctions still logs SPL", "[spl_drain]") {
    // THE DEFECT THIS ROW EXISTS FOR (defect 12). For a route at or past
    // kMaxTransferFunctions (8, RoutingPlan.h:29) the measurement ring is
    // discarded and drainPaired's loop `continue`s past every consumer -- so
    // a tap placed BELOW that check would drop those channels entirely WHILE
    // THEIR SAMPLES WERE BEING CONSUMED, with no Gap, because
    // CaptureBus::dropCount never rises when a ring is drained on purpose.
    // Silence is the one outcome this lane may never ship.
    CaptureBus bus(4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Reference));
    // Nine measurement channels, all naming ch0: route positions 0..8, so
    // position 8 is the FIRST past the cap.
    for (int ch = 1; ch <= 9; ++ch) {
        REQUIRE(bus.config().setRole(ch, ChannelRole::Measurement));
    }
    bus.prepare(48000.0, 10);
    bus.setActive(true);

    AnalysisThread thread(bus, fastConfig());
    // Log the channel at route position 8 AND one below the cap, so the
    // comparison is inside one run.
    const std::array<int, 2> logged{1, 9};
    thread.enableSplLogging(splConfig(48000.0), logged);

    std::array<int, 10> all{};
    for (int i = 0; i < 10; ++i) all[static_cast<std::size_t>(i)] = i;
    pushBlocks(bus, all, 10, 16, 80);

    REQUIRE(waitForSplBlocks(thread, 1, 4, 3000));
    // The one that matters: channel 9 sits at route position 8.
    REQUIRE(waitForSplBlocks(thread, 9, 4, 3000));

    juce::Thread::sleep(50);
    const auto belowCap = thread.splBlockCount(1);
    const auto pastCap = thread.splBlockCount(9);
    INFO("route position 0 (ch 1) blocks = " << belowCap);
    INFO("route position 8 (ch 9) blocks = " << pastCap);
    CHECK(belowCap > 0);
    CHECK(pastCap > 0);
    // Both drains advance together in the same drainPaired call, so the
    // capped route is not merely alive, it keeps up.
    CHECK(pastCap == belowCap);
    // A channel nothing logs stays silent, so the counters are not a global.
    CHECK(thread.splBlockCount(2) == 0);
}

// --- D2: an unrouted session logs with no reference ----------------------

TEST_CASE("D2 an unrouted session logs SPL with no reference channel at all", "[spl_drain]") {
    // Research §C4's ACTUAL requirement. In this path it is met exactly: the
    // measurement channel drains on its own (drainRole) with no reference to
    // wait for. Made red by hanging the feed off drainPaired instead.
    CaptureBus bus(4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    bus.prepare(48000.0, 1);
    bus.setActive(true);

    AnalysisThread thread(bus, fastConfig());
    const std::array<int, 1> logged{0};
    thread.enableSplLogging(splConfig(48000.0), logged);

    const std::array<int, 1> tone{0};
    pushBlocks(bus, tone, 1, 16, 80);

    REQUIRE(waitForSplBlocks(thread, 0, 4, 3000));
    juce::Thread::sleep(50);
    INFO("unrouted blocks = " << thread.splBlockCount(0));
    CHECK(thread.splBlockCount(0) >= 4);
    // Nothing was lost: a session with no reference has no gate to stall on.
    CHECK(thread.splDroppedSamples(0) == 0);
    CHECK_FALSE(rta::meter::hasFlag(thread.splFlagsSeen(0), rta::meter::BlockFlag::Gap));
}

// --- D3: the routed path's gate is reported, with a length ---------------

TEST_CASE("D3 a bounded reference stall loses nothing, and a real bus drop carries its length",
          "[spl_drain]") {
    CaptureBus bus(1024);
    REQUIRE(bus.config().setRole(0, ChannelRole::Reference));
    REQUIRE(bus.config().setRole(1, ChannelRole::Measurement));
    bus.prepare(48000.0, 2);
    bus.setActive(true);

    AnalysisThread thread(bus, fastConfig());
    const std::array<int, 1> logged{1};
    thread.enableSplLogging(splConfig(48000.0), logged);

    // Part one: a BOUNDED stall loses NOTHING. The reference is starved for a
    // while, the paired drain waits, and then it catches up late -- so the
    // ABSENCE of a Gap here is correct, not a hole. This is the half an
    // earlier reading of SPL-R1 would have got wrong by flagging every
    // hiccup.
    const std::array<int, 2> both{0, 1};
    pushBlocks(bus, both, 2, 16, 20);
    REQUIRE(waitForSplBlocks(thread, 1, 4, 3000));
    juce::Thread::sleep(60);
    const auto afterBounded = thread.splBlockCount(1);
    INFO("blocks after a bounded stall = " << afterBounded);
    CHECK(afterBounded > 0);
    CHECK(thread.splDroppedSamples(1) == 0);
    CHECK_FALSE(rta::meter::hasFlag(thread.splFlagsSeen(1), rta::meter::BlockFlag::Gap));

    // Part two: the measurement channel is pushed far past its ring's
    // capacity while the reference is starved, so the BUS itself drops
    // samples on channel 1 and CaptureBus::dropCount rises. Then the
    // reference is fed so the drain can run and produce a block.
    const std::array<int, 1> measurementOnly{1};
    pushBlocks(bus, measurementOnly, 2, 256, 40);  // 10 240 samples into a 1024 ring
    REQUIRE(bus.dropCount(1) > 0);

    pushBlocks(bus, both, 2, 16, 40);
    const auto target = thread.splBlockCount(1) + 1;
    REQUIRE(waitForSplBlocks(thread, 1, target, 3000));
    juce::Thread::sleep(60);

    // Re-read AFTER the wait, not before it: the second push above keeps
    // feeding channel 1, so its ring can overflow again while the drain is
    // catching up, and a count snapshotted earlier is a moving target the
    // session is entitled to have passed.
    const auto busDrops = bus.dropCount(1);
    INFO("CaptureBus::dropCount(1) = " << busDrops);
    INFO("splFlagsSeen(1)      = " << thread.splFlagsSeen(1));
    INFO("splDroppedSamples(1) = " << thread.splDroppedSamples(1));
    // The BIT, and the LENGTH. A log that carried only the bit would admit it
    // lost time and be unable to say how much, and every later reconstructed
    // timestamp would be permanently early (SPL-R1 ∧ SPL-R2, defect 5).
    CHECK(rta::meter::hasFlag(thread.splFlagsSeen(1), rta::meter::BlockFlag::Gap));
    CHECK(thread.splDroppedSamples(1) > 0);
    // NEVER MORE than the bus itself counted. The session reports the bus's
    // own deltas and invents nothing: over-reporting would make a
    // reconstructed timestamp LATE, which is the same defect in the other
    // direction.
    CHECK(thread.splDroppedSamples(1) <= busDrops);
}

// --- D4: the callback is untouched ---------------------------------------

TEST_CASE("D4 nothing under platform/ was touched by this lane", "[spl_drain]") {
    // Asserted here as well as by `git diff main --stat -- platform/` in the
    // PR body, because a test runs on every build and a command in a PR body
    // runs once. The SPL chain lives entirely on the analysis thread; the
    // audio callback still copies into a lock-free ring and does nothing
    // else (CLAUDE.md's real-time rule).
    // The needles are IDENTIFIERS, not the three letters "spl". Measured on
    // this tree: a bare "spl" scan reads 8 files, every one a false positive
    // -- "splice" (five of them, all about not splicing stale audio across a
    // rate change), "split out of AudioIo.cpp", and `isPlaying`. That is
    // SPL-R10's designed-in false positive in a different costume, and a scan
    // that needs its hits explained is a scan nobody will trust the day it
    // goes red for real.
    const auto platformSrc = std::filesystem::path(RTA_REPO_ROOT) / "platform";
    int splMentions = 0;
    int scanned = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(platformSrc)) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".h") continue;
        ++scanned;
        const std::string code = rta::test::codeText(entry.path());
        for (const char* needle : {"splsession", "splmeter", "splconfig", "splblockview",
                                   "blockaccumulator", "blockflag", "meter/block.h"}) {
            if (code.find(needle) != std::string::npos) {
                INFO("platform file names " << needle << ": " << entry.path().string());
                ++splMentions;
            }
        }
    }
    INFO("platform files scanned = " << scanned);
    REQUIRE(scanned > 0);  // a scan that found nothing to read proves nothing
    CHECK(splMentions == 0);
}
