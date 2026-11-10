// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H11 W2: every way analyseWavTransfer says no, and the sentence it says it in.
// The numbers in a sentence are checked against hand-built outcomes (the
// formatting rules of CLAUDE.md "Reading out numbers", D9) and against the
// outcome a real refusal produced (the numbers are the MEASURED ones).
#include "WavTransferFixture.h"

#include "rta/dsp/AverageCount.h"
#include "rta/dsp/DualFftEngine.h"
#include "rta/dsp/Window.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>

using namespace rta::measure;
using namespace wavfix;

namespace {

constexpr std::size_t kFft = 16384;
constexpr std::size_t kHop = 4096;
const std::atomic<bool> kNoCancel{false};

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

WavTransferOutcome run(const MemorySource& source, WavTransferRequest request = {0, 1, 0.0, 0.0, "f.wav"}) {
    return analyseWavTransfer(source, request, kNoCancel);
}

/// The smallest frame count whose effective averages reach the engine's gate,
/// from the SAME function the engine uses -- never hand-derived (record D5).
std::size_t smallestQualifyingFrames() {
    const rta::dsp::Window window(rta::dsp::WindowType::Hann, kFft);
    const double needed = rta::dsp::DualFftEngine::Config{}.minimumEffectiveAverages;
    for (std::size_t frames = 1;; ++frames) {
        if (rta::dsp::fifoEffectiveAverages(window.coefficients(), kHop, frames) >= needed) return frames;
    }
}

}  // namespace

TEST_CASE("W2: channel refusals", "[wavtransfer]") {
    MemorySource mono;
    mono.channels = {pink(100000, 1)};
    auto out = run(mono, {0, 0, 0.0, 0.0, "m.wav"});
    CHECK(out.refusal == WavTransferRefusal::NotEnoughChannels);
    CHECK_FALSE(out.ok());

    const auto two = twoChannel(100000, 0);
    CHECK(run(two, {1, 1, 0.0, 0.0, "s.wav"}).refusal == WavTransferRefusal::SameChannel);
    CHECK(run(two, {0, 2, 0.0, 0.0, "s.wav"}).refusal == WavTransferRefusal::ChannelOutOfRange);
    CHECK(run(two, {-1, 1, 0.0, 0.0, "s.wav"}).refusal == WavTransferRefusal::ChannelOutOfRange);

    auto bad = twoChannel(100000, 0);
    bad.fs = 0.0;
    CHECK(run(bad).refusal == WavTransferRefusal::BadSampleRate);
}

TEST_CASE("W2: ranges outside the file", "[wavtransfer]") {
    const auto source = twoChannel(480000, 0);  // 10 s
    CHECK(run(source, {0, 1, 8.0, 5.0, "r.wav"}).refusal == WavTransferRefusal::RangeOutsideFile);
    CHECK(run(source, {0, 1, 10.0, 0.0, "r.wav"}).refusal == WavTransferRefusal::RangeOutsideFile);
    CHECK(run(source, {0, 1, -1.0, 2.0, "r.wav"}).refusal == WavTransferRefusal::RangeOutsideFile);
    // exactly to the end is inside
    CHECK(run(source, {0, 1, 5.0, 5.0, "r.wav"}).ok());
}

TEST_CASE("W2: the shortest accepted range and one sample fewer (frame count from fifoEffectiveAverages)",
          "[wavtransfer]") {
    const std::size_t frames = smallestQualifyingFrames();
    const std::size_t length = kFft + (frames - 1) * kHop;
    const auto source = twoChannel(length + 50000, 0);
    const auto seconds = [](std::size_t samples) { return static_cast<double>(samples) / kFs; };

    const auto shortest = run(source, {0, 1, 0.0, seconds(length), "s.wav"});
    REQUIRE(shortest.ok());
    CHECK(shortest.frames == frames);
    CHECK(shortest.effectiveAverages >= 8.0);

    // One sample short drops the last frame: frames - 1 reaches less than the gate.
    const auto fewer = run(source, {0, 1, 0.0, seconds(length - 1), "s.wav"});
    CHECK(fewer.refusal == WavTransferRefusal::RangeTooShort);
    CHECK(fewer.reachableAverages < 8.0);
    CHECK(fewer.neededAverages == 8.0);
    const std::string text = wavTransferRefusalText(fewer);
    CHECK(contains(text, "needed"));
    CHECK(contains(text, "8.0"));
}

TEST_CASE("W2: the delay discard counts against the minimum range", "[wavtransfer]") {
    // Exactly the shortest accepted range PLUS the discard of D = 3000 samples
    // passes; one sample less is refused AFTER the delay is known.
    const std::size_t frames = smallestQualifyingFrames();
    const std::size_t length = kFft + (frames - 1) * kHop + 3000;
    const auto source = twoChannel(length + 50000, 3000);
    const auto seconds = [](std::size_t samples) { return static_cast<double>(samples) / kFs; };
    CHECK(run(source, {0, 1, 0.0, seconds(length), "d.wav"}).ok());
    const auto fewer = run(source, {0, 1, 0.0, seconds(length - 1), "d.wav"});
    REQUIRE(fewer.refusal == WavTransferRefusal::RangeTooShort);
    CHECK(fewer.delaySamples == 3000);
    CHECK(fewer.discardedSamples == 3000);
    CHECK(fewer.frames == 0);  // refused before any averaging, not by the engine's gate afterwards
    CHECK(contains(wavTransferRefusalText(fewer), "3000-sample"));
}

TEST_CASE("W2: independent noise has no delay to find", "[wavtransfer]") {
    MemorySource source;
    source.channels = {pink(480000, 1), pink(480000, 2)};
    const auto out = run(source);
    REQUIRE(out.refusal == WavTransferRefusal::NoCorrelation);
    CHECK(out.bestTrust > 0.0);
    CHECK(out.bestTrust < out.trustFloor);
    CHECK(out.trustFloor > 0.0);
}

TEST_CASE("W2: a delay that moves by two samples is refused, by one is not", "[wavtransfer]") {
    constexpr std::size_t n = 3 * (std::size_t{1} << 18);
    MemorySource moved;
    moved.channels.resize(2);
    makePair(n, 5, [](std::size_t i) { return i < n / 2 ? 20 : 22; }, moved.channels[0], moved.channels[1]);
    const auto out = run(moved);
    REQUIRE(out.refusal == WavTransferRefusal::DelayMoved);
    CHECK(out.delayFirst == 20);
    CHECK(out.delayLast == 22);
    const std::string text = wavTransferRefusalText(out);
    CHECK(contains(text, "20 samples (0.4 ms)"));
    CHECK(contains(text, "22 samples (0.5 ms)"));
    CHECK(contains(text, "the system changed or the channels do not share a clock -- pick a shorter range"));
}

TEST_CASE("W2: cancel and read failure", "[wavtransfer]") {
    const auto source = twoChannel(480000, 5);
    const std::atomic<bool> cancelled{true};
    const auto out = analyseWavTransfer(source, {0, 1, 0.0, 0.0, "c.wav"}, cancelled);
    CHECK(out.refusal == WavTransferRefusal::Cancelled);
    CHECK_FALSE(out.ok());

    auto failing = twoChannel(480000, 5);
    failing.failAtOrAfter = 0;  // the delay stage cannot read
    CHECK(run(failing).refusal == WavTransferRefusal::ReadFailed);

    // The delay windows read the first 2^18 samples of a 480000-sample range and
    // the last 2^18; failing from 300000 on spares the first and kills the end window.
    auto late = twoChannel(480000, 5);
    late.failAtOrAfter = 300000;
    CHECK(run(late).refusal == WavTransferRefusal::ReadFailed);
}

TEST_CASE("W2: each refusal sentence carries its measured numbers in the readout format",
          "[wavtransfer]") {
    WavTransferOutcome o;
    o.sampleRate = 44100.0;
    o.fileChannels = 1;
    o.fileLengthSeconds = 10.0;
    const auto say = [&](WavTransferRefusal why) {
        o.refusal = why;
        return wavTransferRefusalText(o);
    };
    CHECK(contains(say(WavTransferRefusal::NotEnoughChannels), "1 channel;"));
    o.fileChannels = 4;
    CHECK(contains(say(WavTransferRefusal::ChannelOutOfRange), "4 channels (ch1 to ch4)"));
    o.referenceChannel = 2;
    CHECK(contains(say(WavTransferRefusal::SameChannel), "ch3"));
    CHECK(contains(say(WavTransferRefusal::BadSampleRate), "44100 Hz"));
    o.requestedStartSeconds = 8.0;
    o.requestedLengthSeconds = 5.0;
    const auto outside = say(WavTransferRefusal::RangeOutsideFile);
    CHECK(contains(outside, "8.00 s"));
    CHECK(contains(outside, "5.00 s"));
    CHECK(contains(outside, "10.00 s long"));
    o.reachableAverages = 6.04;
    o.neededAverages = 8.0;
    o.discardedSamples = 200;
    const auto tooShort = say(WavTransferRefusal::RangeTooShort);
    CHECK(contains(tooShort, "200-sample"));
    CHECK(contains(tooShort, "6.0 effective averages"));
    CHECK(contains(tooShort, "8.0 are needed"));
    o.referenceChannel = 0;
    o.measurementChannel = 2;
    o.bestTrust = 0.0714;
    o.trustFloor = 0.4123;
    const auto none = say(WavTransferRefusal::NoCorrelation);
    CHECK(contains(none, "trust was 0.07 and 0.41 is needed"));
    CHECK(contains(none, "ch1"));
    CHECK(contains(none, "ch3"));
    o.delayFirst = -12;
    o.delayLast = 9;
    const auto moved = say(WavTransferRefusal::DelayMoved);
    CHECK(contains(moved, "-12 samples (-0.3 ms)"));
    CHECK(contains(moved, "9 samples (0.2 ms)"));
    CHECK_FALSE(say(WavTransferRefusal::ReadFailed).empty());
    CHECK_FALSE(say(WavTransferRefusal::Cancelled).empty());
    o.refusal.reset();
    CHECK(wavTransferRefusalText(o).empty());
}

TEST_CASE("W2: trust words are Locate's", "[wavtransfer]") {
    CHECK(std::string(wavTransferTrustWord(0.5)) == "good");
    CHECK(std::string(wavTransferTrustWord(0.49)) == "fair");
    CHECK(std::string(wavTransferTrustWord(0.15)) == "fair");
    CHECK(std::string(wavTransferTrustWord(0.14)) == "poor");
}
