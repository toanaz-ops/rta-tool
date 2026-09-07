// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/DelayLocator.h"
#include "measure/RawCaptureBuffer.h"

#include "rta/gen/Mls.h"
#include "rta/gen/Noise.h"
#include "rta/gen/Prng.h"
#include "rta/platform/OutputRoutingConfig.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace rta::measure;
using namespace rta::platform;

namespace {
constexpr double kSampleRate = 48000.0;
constexpr std::size_t kHopSize = 128;  // several hops before the 480-sample settle threshold
constexpr int kNumChannels = 4;

/// Renders `hopSize` samples through `engine` into scratch output buffers,
/// purely to advance `renderedSamples()` -- what a Locate's excitation
/// would be doing on the OUTPUT side while feedHop() separately receives
/// whatever a (synthetic, here) INPUT capture reports.
void renderOneHop(OutputEngine& engine, std::size_t hopSize) {
    std::vector<std::vector<float>> channels(static_cast<std::size_t>(kNumChannels),
                                              std::vector<float>(hopSize, 0.0f));
    std::vector<float*> ptrs(static_cast<std::size_t>(kNumChannels));
    for (int ch = 0; ch < kNumChannels; ++ch) ptrs[static_cast<std::size_t>(ch)] = channels[static_cast<std::size_t>(ch)].data();
    engine.render(ptrs.data(), kNumChannels, static_cast<int>(hopSize));
}

DelayLocator::Config baseConfig(std::size_t captureLength) {
    DelayLocator::Config config;
    config.outputChannel = 1;
    config.captureLength = captureLength;
    config.options.sampleRate = kSampleRate;
    return config;
}
}  // namespace

TEST_CASE("the accumulator yields exactly L", "[delay][locator][rawcapture]") {
    RawCaptureBuffer buffer;
    buffer.arm(4096);

    std::vector<float> refA(300, 1.0f), measA(300, -1.0f);
    std::vector<float> refB(3000, 2.0f), measB(3000, -2.0f);
    std::vector<float> refC(2000, 3.0f), measC(2000, -3.0f);  // overshoots by 1204

    buffer.feedHop(refA, measA);
    REQUIRE_FALSE(buffer.isFull());
    buffer.feedHop(refB, measB);
    REQUIRE_FALSE(buffer.isFull());
    buffer.feedHop(refC, measC);
    REQUIRE(buffer.isFull());

    REQUIRE(buffer.reference().size() == 4096);
    REQUIRE(buffer.measurement().size() == 4096);
    // In order, no gap, no double-count: the first 300 read 1.0/-1.0, the
    // next 3000 read 2.0/-2.0, the remaining 796 (of C's 2000) read 3.0/-3.0.
    REQUIRE(buffer.reference()[0] == 1.0f);
    REQUIRE(buffer.reference()[299] == 1.0f);
    REQUIRE(buffer.reference()[300] == 2.0f);
    REQUIRE(buffer.reference()[3299] == 2.0f);
    REQUIRE(buffer.reference()[3300] == 3.0f);
    REQUIRE(buffer.reference()[4095] == 3.0f);
    REQUIRE(buffer.measurement()[0] == -1.0f);
    REQUIRE(buffer.measurement()[4095] == -3.0f);
}

TEST_CASE("the accumulator does not allocate mid-capture", "[delay][locator][rawcapture]") {
    // Same PROPERTY test_average_group.cpp's T12 measures with a counting
    // global operator new -- not reused here because that override is
    // already installed, once, for this whole test BINARY
    // (rtatool_analysis_tests), and a second definition would be a link
    // error. Pointer stability is the direct, allocator-agnostic proof for
    // ONE class's own buffers: if `arm()` is the only allocation, the
    // vector's storage address never moves across `feedHop()`.
    RawCaptureBuffer buffer;
    buffer.arm(4096);
    const float* refBefore = buffer.reference().data();
    const float* measBefore = buffer.measurement().data();

    std::vector<float> ref(512, 0.5f), meas(512, -0.5f);
    for (int i = 0; i < 8; ++i) buffer.feedHop(ref, meas);

    REQUIRE(buffer.isFull());
    CHECK(buffer.reference().data() == refBefore);
    CHECK(buffer.measurement().data() == measBefore);
}

TEST_CASE("Locate drives OutputEngine in order: arm, capture, disarm", "[delay][locator]") {
    OutputEngine engine;
    engine.prepare(kSampleRate, kNumChannels);

    DelayLocator locator(engine, rta::gen::PinkNoise(rta::gen::Pcg32(1234, 1), -12.0),
                          baseConfig(8192));
    REQUIRE(locator.lastRefusal() == LocateRefusal::None);
    REQUIRE(locator.state() == LocateState::Idle);

    locator.arm();
    REQUIRE(locator.state() == LocateState::Waiting);
    REQUIRE_FALSE(engine.sourceIsQuiescent());  // armed: not quiescent

    // Exactly one routed output -- soloOutput's whole point (record sec.3).
    for (int ch = 0; ch < kMaxChannels; ++ch) {
        const auto expected = (ch == 1) ? OutputRole::Routed : OutputRole::None;
        REQUIRE(engine.role(ch) == expected);
    }

    rta::gen::Pcg32 xRng(4321, 1);
    rta::gen::WhiteNoise xSource(xRng, -12.0);
    std::vector<float> x(8192 + 1024);
    xSource.process(x);
    std::vector<float> y(x.size(), 0.0f);
    for (std::size_t n = 300; n < x.size(); ++n) y[n] = x[n - 300];

    std::size_t offset = 0;
    while (locator.state() != LocateState::Done) {
        renderOneHop(engine, kHopSize);
        const std::size_t take = std::min(kHopSize, x.size() - offset);
        std::span<const float> refHop(x.data() + offset, take);
        std::span<const float> measHop(y.data() + offset, take);
        locator.feedHop(refHop, measHop);
        offset += take;
        REQUIRE(offset <= x.size());  // the fixture is sized with headroom
    }

    REQUIRE(locator.suggestion().has_value());

    // disarmSource() ran before suggestDelay -- observable because the
    // engine's armed flag is already false by the time Done is reached
    // (feedHop's own body calls disarmSource() strictly before suggestDelay,
    // so if a suggestion exists at all, disarm already happened).
    for (int i = 0; i < 20; ++i) renderOneHop(engine, kHopSize);  // let the ramp settle
    REQUIRE(engine.sourceIsQuiescent());
}

TEST_CASE("Locate refuses a periodic Mls excitation, accepts PinkNoise",
          "[delay][locator]") {
    OutputEngine engine;
    engine.prepare(kSampleRate, kNumChannels);

    DelayLocator mlsLocator(engine, rta::gen::Mls(15, -12.0), baseConfig(4096));
    REQUIRE(mlsLocator.lastRefusal() == LocateRefusal::PeriodicExcitation);
    mlsLocator.arm();
    REQUIRE(mlsLocator.state() == LocateState::Idle);  // arm() is a no-op after a refusal

    DelayLocator pinkLocator(engine, rta::gen::PinkNoise(rta::gen::Pcg32(2, 1), -12.0),
                              baseConfig(4096));
    REQUIRE(pinkLocator.lastRefusal() == LocateRefusal::None);
}

TEST_CASE("end-to-end on a synthetic pair", "[delay][locator]") {
    OutputEngine engine;
    engine.prepare(kSampleRate, kNumChannels);
    constexpr std::size_t kCaptureLength = 2048;
    constexpr std::ptrdiff_t kDelay = 50;
    DelayLocator locator(engine, rta::gen::PinkNoise(rta::gen::Pcg32(99, 1), -12.0),
                          baseConfig(kCaptureLength));
    locator.arm();

    rta::gen::Pcg32 xRng(555, 1);
    rta::gen::WhiteNoise xSource(xRng, -12.0);
    std::vector<float> x(kCaptureLength + 1024);
    xSource.process(x);
    std::vector<float> y(x.size(), 0.0f);
    for (std::size_t n = static_cast<std::size_t>(kDelay); n < x.size(); ++n) {
        y[n] = x[n - static_cast<std::size_t>(kDelay)];
    }

    // A loud, UNRELATED transient in [0, 384) -- standing in for "the first
    // 10 ms are not stationary" (record sec.11) the settle wait exists to
    // skip. With kHopSize=128 and the 480-sample settle threshold, the
    // first CAPTURED hop under correct behaviour starts at index 384 (the
    // hop that crosses 480 samples rendered), so a correct capture never
    // sees this transient at all. The true delay (50 samples) sits WELL
    // inside [0, 384), so the transient overwrites real correlated content,
    // not silent lead-in -- destroying enough of the true peak's support
    // (given the short 2048-sample capture) to move the mutated answer.
    rta::gen::Pcg32 junkRng(777, 1);
    rta::gen::WhiteNoise junkSource(junkRng, 12.0);  // loud: +12 dBFS RMS
    junkSource.process(std::span<float>(y.data(), 384));

    std::size_t offset = 0;
    while (locator.state() != LocateState::Done) {
        renderOneHop(engine, kHopSize);
        const std::size_t take = std::min(kHopSize, x.size() - offset);
        locator.feedHop(std::span<const float>(x.data() + offset, take),
                         std::span<const float>(y.data() + offset, take));
        offset += take;
        REQUIRE(offset <= x.size());
    }

    REQUIRE(locator.suggestion().has_value());
    REQUIRE(locator.suggestion()->best.delaySamples == kDelay);
    REQUIRE(locator.suggestion()->verdict == rta::dsp::DelayVerdict::Accepted);
}
