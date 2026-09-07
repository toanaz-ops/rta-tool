// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-OUT (docs/plans/2026-09-07-L7-out-impl-plan.md task B; decision record
// docs/dsp/2026-09-06-l7-output-path.md sec.4, sec.6, sec.10, task B1-B8).
// Every acceptance number here is a closed-form identity over already-
// golden'd rta::gen sources (record sec.10: "no new golden vector is
// needed") -- OutputEngine lives in rta_platform_types, so this whole file
// is provable with no device.

#include "rta/platform/OutputEngine.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::gen::Oscillator;
using rta::gen::RampedGain;
using rta::gen::Sweep;
using rta::platform::kMaxChannels;
using rta::platform::OutputEngine;
using rta::platform::OutputRole;
using rta::platform::SourceVariant;

namespace {

constexpr double kFs = 48000.0;

std::vector<float*> channelPointers(std::vector<std::vector<float>>& buffers) {
    std::vector<float*> ptrs;
    ptrs.reserve(buffers.size());
    for (auto& channel : buffers) ptrs.push_back(channel.data());
    return ptrs;
}

}  // namespace

TEST_CASE("OutputEngine idle contract: nothing armed renders exact silence; a null buffer is a no-op") {
    OutputEngine engine;
    engine.prepare(kFs, 2);

    // Poisoned with a non-zero value so a clear that never runs would be caught.
    std::vector<std::vector<float>> buffers(2, std::vector<float>(512, 1.0f));
    auto ptrs = channelPointers(buffers);
    engine.render(ptrs.data(), 2, 512);

    for (const auto& channel : buffers) {
        for (float s : channel) CHECK(s == 0.0f);
    }
    CHECK(engine.renderedSamples() == 0);

    engine.render(nullptr, 2, 512);
    CHECK(engine.renderedSamples() == 0);
}

TEST_CASE("OutputEngine: closed-form ramped sine through a single routed output") {
    OutputEngine engine;
    engine.prepare(kFs, 4);

    constexpr double freq = 1000.0;
    constexpr double levelDb = -6.0;
    const double amplitude = std::pow(10.0, levelDb / 20.0);

    REQUIRE(engine.setSource(Oscillator(kFs, freq, levelDb)));
    REQUIRE(engine.routeOutput(1, true));
    engine.armSource();

    // Routing and arming land in the same block: master and the per-output
    // gate for channel 1 both start Idle -> Rising together, so the product
    // is g(n)^2 for the first 480 samples (record sec.10 T2).
    auto expectedGate = [](int n) -> double {
        if (n >= 480) return 1.0;
        const double p = double(n) / 480.0;
        const double g = 0.5 * (1.0 - std::cos(std::numbers::pi * p));
        return g * g;
    };

    std::vector<std::vector<float>> block1(4, std::vector<float>(480, 0.0f));
    auto ptrs1 = channelPointers(block1);
    engine.render(ptrs1.data(), 4, 480);

    std::vector<std::vector<float>> block2(4, std::vector<float>(512, 0.0f));
    auto ptrs2 = channelPointers(block2);
    engine.render(ptrs2.data(), 4, 512);

    for (int n = 0; n < 480; ++n) {
        const double expected =
            amplitude * std::sin(2.0 * std::numbers::pi * freq * n / kFs) * expectedGate(n);
        CHECK_THAT(double(block1[1][static_cast<std::size_t>(n)]), WithinAbs(expected, 1e-6));
        CHECK(block1[0][static_cast<std::size_t>(n)] == 0.0f);
        CHECK(block1[2][static_cast<std::size_t>(n)] == 0.0f);
        CHECK(block1[3][static_cast<std::size_t>(n)] == 0.0f);
    }
    for (int local = 0; local < 512; ++local) {
        const int n = 480 + local;
        const double expected =
            amplitude * std::sin(2.0 * std::numbers::pi * freq * n / kFs) * expectedGate(n);
        CHECK_THAT(double(block2[1][static_cast<std::size_t>(local)]), WithinAbs(expected, 1e-6));
        CHECK(block2[0][static_cast<std::size_t>(local)] == 0.0f);
    }
}

TEST_CASE("OutputEngine: complementary handover keeps the summed drive constant") {
    OutputEngine engine;
    engine.prepare(kFs, 3);
    REQUIRE(engine.setSource(Oscillator(kFs, 1000.0, -6.0)));

    // Channel 2: the reference, routed and driven Running before the
    // handover block.
    REQUIRE(engine.routeOutput(2, true));
    engine.armSource();
    std::vector<std::vector<float>> warm1(3, std::vector<float>(480, 0.0f));
    auto warm1Ptrs = channelPointers(warm1);
    engine.render(warm1Ptrs.data(), 3, 480);  // channel 2's gate, and master, reach Running

    // Channel 0: routed and driven Running too -- "steady as a reference copy".
    REQUIRE(engine.routeOutput(0, true));
    std::vector<std::vector<float>> warm2(3, std::vector<float>(480, 0.0f));
    auto warm2Ptrs = channelPointers(warm2);
    engine.render(warm2Ptrs.data(), 3, 480);

    // The handover, all in ONE block: channel 1 starts rising, channel 0
    // starts falling, channel 2 is untouched.
    REQUIRE(engine.routeOutput(1, true));
    REQUIRE(engine.routeOutput(0, false));

    std::vector<std::vector<float>> handover(3, std::vector<float>(480, 0.0f));
    auto handoverPtrs = channelPointers(handover);
    engine.render(handoverPtrs.data(), 3, 480);

    for (std::size_t n = 0; n < 480; ++n) {
        const double sum = double(handover[0][n]) + double(handover[1][n]);
        CHECK_THAT(sum, WithinAbs(double(handover[2][n]), 1e-6));
    }
}

TEST_CASE("OutputEngine: routing bounds are checked against [0,kMaxChannels); render bounds against what it received") {
    OutputEngine engine;
    engine.prepare(kFs, 2);

    CHECK_FALSE(engine.routeOutput(-1, true));
    CHECK_FALSE(engine.routeOutput(kMaxChannels, true));
    CHECK(engine.role(-1) == OutputRole::None);
    CHECK(engine.role(kMaxChannels) == OutputRole::None);

    REQUIRE(engine.setSource(Oscillator(kFs, 1000.0, -6.0)));
    REQUIRE(engine.routeOutput(5, true));  // accepted: inside [0, kMaxChannels)
    engine.armSource();

    // Render with only 2 channels received this block -- channel 5's gate
    // must never be touched, and nothing writes past these two buffers.
    std::vector<std::vector<float>> buffers(2, std::vector<float>(256, 0.0f));
    auto ptrs = channelPointers(buffers);
    engine.render(ptrs.data(), 2, 256);

    for (float s : buffers[0]) CHECK(s == 0.0f);
    for (float s : buffers[1]) CHECK(s == 0.0f);
    CHECK(engine.role(5) == OutputRole::Routed);  // the routing table itself is unaffected
}

TEST_CASE("OutputEngine: setSource is refused while armed, phase stays continuous, and the swap succeeds once quiescent") {
    OutputEngine engine;
    engine.prepare(kFs, 1);

    REQUIRE(engine.setSource(Oscillator(kFs, 1000.0, -6.0)));
    REQUIRE(engine.routeOutput(0, true));
    engine.armSource();

    std::vector<float> buf(600, 0.0f);
    float* ptrs[] = {buf.data()};
    engine.render(ptrs, 1, 600);  // past the 480-sample rise: Running

    CHECK_FALSE(engine.sourceIsQuiescent());
    CHECK_FALSE(engine.setSource(Oscillator(kFs, 2000.0, -6.0)));

    // Phase stays continuous across the refusal: the next block is still the
    // SAME 1000 Hz tone continuing from sample 600, not reset to phase 0.
    std::vector<float> buf2(64, 0.0f);
    float* ptrs2[] = {buf2.data()};
    engine.render(ptrs2, 1, 64);
    const double amplitude = std::pow(10.0, -6.0 / 20.0);
    for (std::size_t n = 0; n < 64; ++n) {
        const double sampleIndex = double(600 + n);
        const double expected = amplitude * std::sin(2.0 * std::numbers::pi * 1000.0 * sampleIndex / kFs);
        CHECK_THAT(double(buf2[n]), WithinAbs(expected, 1e-6));
    }

    engine.disarmSource();
    std::vector<float> drain(480, 0.0f);  // one full 10 ms ramp
    float* drainPtrs[] = {drain.data()};
    engine.render(drainPtrs, 1, 480);

    CHECK(engine.sourceIsQuiescent());
    CHECK(engine.setSource(Oscillator(kFs, 2000.0, -6.0)));

    REQUIRE(engine.routeOutput(0, true));
    engine.armSource();
    std::vector<float> first(1, 0.0f);
    float* firstPtrs[] = {first.data()};
    engine.render(firstPtrs, 1, 1);
    CHECK_THAT(double(first[0]), WithinAbs(0.0, 1e-6));  // g(0) = 0
}

TEST_CASE("OutputEngine: chunking through scratch is invisible -- sample-exact against a single-pass reference") {
    OutputEngine engine;
    engine.prepare(kFs, 1);
    REQUIRE(engine.setSource(Oscillator(kFs, 1000.0, -6.0)));
    REQUIRE(engine.routeOutput(0, true));
    engine.armSource();

    const int n = static_cast<int>(3 * OutputEngine::kScratchCapacity) + 17;
    std::vector<float> actual(static_cast<std::size_t>(n), 0.0f);
    float* ptrs[] = {actual.data()};
    engine.render(ptrs, 1, n);

    // Reference: the SAME float operations in the SAME order -- master gain
    // applied to the whole signal, then the per-output gate -- built from a
    // standalone Oscillator and two RampedGains with no chunking at all.
    Oscillator refOsc(kFs, 1000.0, -6.0);
    RampedGain refMaster(kFs);
    RampedGain refGate(kFs);
    refMaster.requestOn();
    refGate.requestOn();

    std::vector<float> expected(static_cast<std::size_t>(n), 0.0f);
    refOsc.process(expected);
    for (float& sample : expected) sample *= refMaster.nextGain();
    for (float& sample : expected) sample *= refGate.nextGain();

    for (int i = 0; i < n; ++i) {
        CHECK(actual[static_cast<std::size_t>(i)] == expected[static_cast<std::size_t>(i)]);
    }
}

TEST_CASE("OutputEngine: a sweep ends at zero, and renderedSamples() counts exactly what was rendered since arm") {
    OutputEngine engine;
    engine.prepare(kFs, 1);

    Sweep::Config cfg;
    cfg.sampleRate = kFs;
    cfg.durationSec = 0.1;
    Sweep probe(cfg);
    const std::size_t sweepLen = probe.lengthSamples();

    REQUIRE(engine.setSource(SourceVariant(std::in_place_type<Sweep>, cfg)));
    REQUIRE(engine.routeOutput(0, true));
    engine.armSource();

    const std::size_t total = sweepLen + 1000;
    std::vector<float> buf(total, 0.0f);
    float* ptrs[] = {buf.data()};
    engine.render(ptrs, 1, static_cast<int>(total));

    for (std::size_t n = sweepLen; n < total; ++n) {
        CHECK(buf[n] == 0.0f);
    }
    CHECK(engine.renderedSamples() == total);
}

TEST_CASE("OutputEngine: prepare() at a new rate bumps outputEpoch and disarms; a stale re-arm renders silence until setSource runs again") {
    OutputEngine engine;
    engine.prepare(kFs, 1);
    REQUIRE(engine.setSource(Oscillator(kFs, 1000.0, -6.0)));
    REQUIRE(engine.routeOutput(0, true));
    engine.armSource();

    std::vector<float> buf(600, 0.0f);
    float* ptrs[] = {buf.data()};
    engine.render(ptrs, 1, 600);
    bool anyNonzero = false;
    for (std::size_t i = buf.size() - 100; i < buf.size(); ++i) {
        if (std::abs(buf[i]) > 1e-4f) anyNonzero = true;
    }
    CHECK(anyNonzero);

    const std::uint64_t epochBefore = engine.outputEpoch();
    engine.prepare(96000.0, 1);
    CHECK(engine.outputEpoch() == epochBefore + 1);

    // Stale re-arm: armSource() with no fresh setSource() after the rate
    // change. The recorded slotEpoch_ no longer matches outputEpoch(), so
    // render() must produce silence regardless.
    engine.armSource();
    std::vector<float> staleBuf(64, 1.0f);  // poisoned
    float* stalePtrs[] = {staleBuf.data()};
    engine.render(stalePtrs, 1, 64);
    for (float s : staleBuf) CHECK(s == 0.0f);

    // Recovery: disarm, quiescence follows immediately (the gates were
    // already Idle from prepare()), then setSource() succeeds and playback
    // resumes.
    engine.disarmSource();
    CHECK(engine.sourceIsQuiescent());
    REQUIRE(engine.setSource(Oscillator(96000.0, 1000.0, -6.0)));
    REQUIRE(engine.routeOutput(0, true));
    engine.armSource();

    std::vector<float> resumed(1200, 0.0f);
    float* resumedPtrs[] = {resumed.data()};
    engine.render(resumedPtrs, 1, 1200);
    bool resumedNonzero = false;
    for (std::size_t i = resumed.size() - 100; i < resumed.size(); ++i) {
        if (std::abs(resumed[i]) > 1e-4f) resumedNonzero = true;
    }
    CHECK(resumedNonzero);
}
