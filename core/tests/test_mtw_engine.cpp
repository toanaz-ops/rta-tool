// SPDX-License-Identifier: AGPL-3.0-or-later
//
// T2/T5 from docs/dsp/2026-09-05-mtw-l3.md §7, at the test length derived in
// docs/plans/2026-09-05-L3-mtw-impl-plan.md ("The test signal length..."):
// n = 327680 gives every band exactly 16 frames held in its FIFO (Neff =
// 8.5866271 in every band, over the coherence gate of 8), and n - N_k is an
// exact multiple of hop_k in every band, so every frame count below is a
// stated integer, not a floor a reader has to trust.

#include "rta/dsp/MtwEngine.h"
#include "rta/dsp/MtwResult.h"
#include "rta/gen/Noise.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace rta::dsp;

namespace {

constexpr std::size_t kFullLength = 327680;   // 17 frames at N=65536
constexpr std::size_t kShortLength = 180224;  // 8 frames at N=65536

std::vector<float> whiteNoise(std::size_t n, std::uint64_t seed) {
    rta::gen::WhiteNoise src(rta::gen::Pcg32(seed, 0x9E3779B9u));
    std::vector<float> out(n);
    src.process(out);
    return out;
}

/// y[i] = x[i - delay], leading `delay` samples zero -- a real delay line
/// powering on, and record §7 T2's "compensated pure delay" fixture.
std::vector<float> delayedCopy(const std::vector<float>& x, std::size_t delay) {
    std::vector<float> y(x.size(), 0.0f);
    for (std::size_t i = delay; i < x.size(); ++i) y[i] = x[i - delay];
    return y;
}

void checkUnityAcrossEveryBand(const MtwEngine& engine) {
    const auto result = makeMtwResult(engine, Estimator::H1);
    std::size_t checked = 0;
    for (std::size_t i = 0; i < result.frequencyHz.size(); ++i) {
        const std::size_t b = bandForIndex(result, i);
        const auto& band = result.bands[b];
        const std::size_t localBin = band.firstBin + (i - band.firstIndex);
        if (!(engine.band(b).referencePsd()[localBin] > 0.0)) continue;
        ++checked;
        CAPTURE(i, b, localBin);
        REQUIRE(result.h[i].real() == Catch::Approx(1.0).margin(1e-12));
        REQUIRE(result.h[i].imag() == 0.0);
        REQUIRE(result.magnitudeDb[i] == Catch::Approx(0.0).margin(1e-9));
        REQUIRE(result.phaseRadians[i] == 0.0f);
        const auto coherence = coherenceAt(result, i);
        REQUIRE(coherence.has_value());
        REQUIRE(*coherence == Catch::Approx(1.0f).margin(1e-12));
    }
    REQUIRE(checked == result.frequencyHz.size());  // every bin qualified -- not vacuous
}

}  // namespace

TEST_CASE("a compensated pure delay reads unity across every band", "[mtw][engine]") {
    constexpr int kDelay = 137;
    const auto x = whiteNoise(kFullLength, 11);
    const auto y = delayedCopy(x, kDelay);

    MtwConfig cfg;
    cfg.referenceDelaySamples = kDelay;
    MtwEngine engine(cfg);
    engine.process(x, y);

    checkUnityAcrossEveryBand(engine);
}

TEST_CASE("a delay divisible by no power of two still reads unity", "[mtw][engine]") {
    // Record §7 T2: odd, and larger than the hop of several bands -- the case
    // a decimated design could not have passed without a fractional delay.
    constexpr int kDelay = 4099;
    const auto x = whiteNoise(kFullLength, 13);
    const auto y = delayedCopy(x, kDelay);

    MtwConfig cfg;
    cfg.referenceDelaySamples = kDelay;
    MtwEngine engine(cfg);
    engine.process(x, y);

    checkUnityAcrossEveryBand(engine);
}

TEST_CASE("coherence is withheld per band until that band has its own 16 frames",
          "[mtw][engine]") {
    const auto x = whiteNoise(kFullLength, 17);

    MtwConfig cfg;
    MtwEngine engine(cfg);
    engine.process(std::span<const float>(x.data(), kShortLength),
                    std::span<const float>(x.data(), kShortLength));

    {
        const auto result = makeMtwResult(engine, Estimator::H1);
        REQUIRE_FALSE(result.bandSnapshots[0].coherence.has_value());
        for (std::size_t i = 0; i < 256; ++i) {
            REQUIRE_FALSE(coherenceAt(result, i).has_value());
        }
        for (std::size_t i = 256; i < result.frequencyHz.size(); ++i) {
            CAPTURE(i);
            REQUIRE(coherenceAt(result, i).has_value());
        }
    }

    engine.process(std::span<const float>(x.data() + kShortLength, kFullLength - kShortLength),
                    std::span<const float>(x.data() + kShortLength, kFullLength - kShortLength));

    const auto result = makeMtwResult(engine, Estimator::H1);
    for (std::size_t b = 0; b < result.bandSnapshots.size(); ++b) {
        CAPTURE(b);
        REQUIRE(result.bandSnapshots[b].coherence.has_value());
    }
    REQUIRE(result.bandSnapshots[0].effectiveAverages == Catch::Approx(8.5866271).epsilon(1e-6));
}

TEST_CASE("every band advances by its own hop, and reports its own integration seconds",
          "[mtw][engine]") {
    const auto x = whiteNoise(kFullLength, 19);
    MtwConfig cfg;
    MtwEngine engine(cfg);
    engine.process(x, x);

    const std::vector<std::size_t> expectedFrames{17, 37, 77, 157, 317, 637, 1277};
    const std::vector<double> expectedSeconds{5.4613333, 2.7306667, 1.3653333, 0.6826667,
                                               0.3413333, 0.1706667, 0.0853333};
    const auto bands = mtwBands(cfg);
    for (std::size_t i = 0; i < engine.bandCount(); ++i) {
        CAPTURE(i);
        REQUIRE(engine.band(i).frameCount() == expectedFrames[i]);
        REQUIRE(engine.band(i).effectiveAverages() == Catch::Approx(8.5866271).epsilon(1e-6));
        REQUIRE(engine.band(i).effectiveAverages() ==
                Catch::Approx(engine.band(0).effectiveAverages()).epsilon(1e-9));
        REQUIRE(bands[i].integrationSeconds == Catch::Approx(expectedSeconds[i]).margin(1e-7));
        if (i > 0) {
            REQUIRE(bands[i - 1].integrationSeconds / bands[i].integrationSeconds ==
                    Catch::Approx(2.0).epsilon(1e-6));
        }
    }
}

TEST_CASE("reset re-arms the reference delay in every band", "[mtw][engine]") {
    constexpr int kDelay = 137;
    const auto x = whiteNoise(kFullLength, 23);
    const auto y = delayedCopy(x, kDelay);

    MtwConfig cfg;
    cfg.referenceDelaySamples = kDelay;
    MtwEngine engine(cfg);

    // 2 seconds of the pair, then reset -- the transient state must not
    // survive.
    const std::size_t warmup = static_cast<std::size_t>(2.0 * cfg.sampleRate);
    engine.process(std::span<const float>(x.data(), warmup), std::span<const float>(y.data(), warmup));
    engine.reset();

    // The FULL length again: reset() zeroes frameCount_ in every band, so the
    // bottom band needs its 16 frames again before its gate reopens.
    engine.process(x, y);
    checkUnityAcrossEveryBand(engine);
}

TEST_CASE("the stitched result exposes one snapshot per band, in order", "[mtw][engine]") {
    const auto x = whiteNoise(kFullLength, 29);
    MtwConfig cfg;
    MtwEngine engine(cfg);
    engine.process(x, x);

    const auto result = makeMtwResult(engine, Estimator::H1);
    REQUIRE(result.bandSnapshots.size() == result.bands.size());
    REQUIRE(result.bandSnapshots.size() == 7);

    for (std::size_t i = 0; i < result.bands.size(); ++i) {
        CAPTURE(i);
        REQUIRE(result.bandSnapshots[i].binWidthHz ==
                Catch::Approx(cfg.sampleRate / static_cast<double>(result.bands[i].fftSize))
                    .epsilon(1e-12));
    }

    REQUIRE(bandForIndex(result, 0) == 0);
    REQUIRE(bandForIndex(result, 255) == 0);
    REQUIRE(bandForIndex(result, 256) == 1);
    REQUIRE(bandForIndex(result, 1280) == 6);

    for (std::size_t i = 0; i < engine.bandCount(); ++i) {
        REQUIRE(engine.band(i).config().fifoDepth == 16);
        const auto bands = mtwBands(cfg);
        REQUIRE(engine.band(i).config().timeConstantSeconds ==
                Catch::Approx(16.0 * static_cast<double>(bands[i].hopSize) / cfg.sampleRate)
                    .epsilon(1e-12));
    }

    // Memory rule 1 (record §2): exponential mode must not carry the FIFO's
    // dead weight, so every band's fifoDepth is forced to 1.
    MtwConfig expCfg;
    expCfg.averaging = TransferAveraging::Exponential;
    MtwEngine expEngine(expCfg);
    for (std::size_t i = 0; i < expEngine.bandCount(); ++i) {
        CAPTURE(i);
        REQUIRE(expEngine.band(i).config().fifoDepth == 1);
    }

    REQUIRE_THROWS_AS(engine.process(std::span<const float>(x.data(), 10),
                                      std::span<const float>(x.data(), 11)),
                       std::invalid_argument);
}
