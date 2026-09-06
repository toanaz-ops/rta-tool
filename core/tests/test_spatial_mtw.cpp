// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Both cases run on the DEFAULT MTW table (N0=1024, K=6, 7 bands) at
// kFullLength = 327680 -- the same length test_mtw_engine.cpp uses to put
// every band at exactly 16 FIFO frames (effectiveAverages == 8.5866271,
// quoted from that file's own literal, never re-derived from a header
// formula: memory/a-header-ceiling-is-not-the-reachable-average-count.md).
// A real MtwEngine is in the path here (record §11 T7), so magnitude
// tolerances use the two-term float32 form of memory/float32-fft-precision.md
// rather than a bare relative epsilon.

#include "rta/dsp/MtwEngine.h"
#include "rta/dsp/SpatialMtw.h"
#include "rta/gen/Noise.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

using namespace rta::dsp;

namespace {

constexpr std::size_t kFullLength = 327680;

std::vector<float> whiteNoise(std::size_t n, std::uint64_t seed) {
    rta::gen::WhiteNoise src(rta::gen::Pcg32(seed, 0x9E3779B9u));
    std::vector<float> out(n);
    src.process(out);
    return out;
}

/// y[i] = x[i - delay], leading `delay` samples zero -- same fixture as
/// test_mtw_engine.cpp's delayedCopy().
std::vector<float> delayedCopy(const std::vector<float>& x, std::size_t delay) {
    std::vector<float> y(x.size(), 0.0f);
    for (std::size_t i = delay; i < x.size(); ++i) y[i] = x[i - delay];
    return y;
}

}  // namespace

TEST_CASE("two identical positions average to either input, band by band", "[spatial][mtw]") {
    constexpr int kDelay = 137;
    const auto x = whiteNoise(kFullLength, 61);
    const auto y = delayedCopy(x, kDelay);

    MtwConfig cfg;
    cfg.referenceDelaySamples = kDelay;
    MtwEngine engineA(cfg);
    engineA.process(x, y);
    MtwEngine engineB(cfg);
    engineB.process(x, y);

    const auto resultA = makeMtwResult(engineA, Estimator::H1);
    const auto resultB = makeMtwResult(engineB, Estimator::H1);
    REQUIRE(resultA.frequencyHz.size() == 1281);  // record §11 T7

    const std::vector<MtwResult> positions{resultA, resultB};
    const std::vector<double> u{1.0, 1.0};
    const auto combined = spatialAverageMtw(positions, u);
    REQUIRE(combined.has_value());
    REQUIRE(combined->frequencyHz.size() == 1281);

    const auto bandLayout = mtwBands(cfg);
    for (std::size_t b = 0; b < bandLayout.size(); ++b) {
        CAPTURE(b);
        const auto& band = bandLayout[b];
        REQUIRE(resultA.bandSnapshots[b].coherence.has_value());  // gate cleared at kFullLength

        double peak = 0.0;
        for (std::size_t bin = band.firstBin; bin <= band.lastBin; ++bin) {
            peak = std::max(peak, std::abs(static_cast<double>(resultA.bandSnapshots[b].magnitudeDb[bin])));
        }

        for (std::size_t bin = band.firstBin; bin <= band.lastBin; ++bin) {
            CAPTURE(bin);
            const double expected = resultA.bandSnapshots[b].magnitudeDb[bin];
            const double tolerance = std::abs(expected) * 1.0e-5 + peak * 1.0e-6;
            REQUIRE(combined->bands[b].magnitudeDb[bin] == Catch::Approx(expected).margin(tolerance));
            REQUIRE(combined->bands[b].phaseAgreement[bin] == Catch::Approx(1.0).margin(1e-6));
            REQUIRE(combined->bands[b].bins[bin].contributors == 2);
        }
    }
}

TEST_CASE("one sample of uncompensated spacing gives the top band a cosine agreement",
          "[spatial][mtw]") {
    constexpr int kDelay = 137;
    const auto x = whiteNoise(kFullLength, 67);
    const auto yA = delayedCopy(x, static_cast<std::size_t>(kDelay));
    const auto yB = delayedCopy(x, static_cast<std::size_t>(kDelay) + 1);

    MtwConfig cfg;
    cfg.referenceDelaySamples = kDelay;  // BOTH engines compensate D, never D+1
    MtwEngine engineA(cfg);
    engineA.process(x, yA);
    MtwEngine engineB(cfg);
    engineB.process(x, yB);

    const auto resultA = makeMtwResult(engineA, Estimator::H1);
    const auto resultB = makeMtwResult(engineB, Estimator::H1);

    const std::vector<MtwResult> positions{resultA, resultB};
    const std::vector<double> u{1.0, 1.0};
    const auto combined = spatialAverageMtw(positions, u);
    REQUIRE(combined.has_value());

    const auto bandLayout = mtwBands(cfg);
    const std::size_t topIndex = bandLayout.size() - 1;
    const auto& topBand = bandLayout[topIndex];
    REQUIRE(topBand.fftSize == cfg.topFftSize);  // the top band IS the N0 table

    const double fs = cfg.sampleRate;
    for (std::size_t bin = topBand.firstBin; bin <= topBand.lastBin; ++bin) {
        CAPTURE(bin);
        const double freq = static_cast<double>(bin) * fs / static_cast<double>(topBand.fftSize);
        // H_B/H_A = exp(-i*2*pi*f/fs) for the residual 1-sample lag; the
        // averaged z = 0.5*(1 + exp(-i*theta)) with theta = 2*pi*f/fs has
        // |z| = |cos(theta/2)| = |cos(pi*f/fs)| and arg(z) = -theta/2 --
        // the derivation quoted in docs/dsp/2026-09-06-multichannel-l6b.md
        // §11 T7.
        const double halfTheta = std::numbers::pi * freq / fs;
        const double expectedR = std::abs(std::cos(halfTheta));
        const double expectedPhase = -halfTheta;

        const double actualR = combined->bands[topIndex].phaseAgreement[bin];
        const double actualPhase = combined->bands[topIndex].phaseRadians[bin];

        // Two-term tolerance (memory/float32-fft-precision.md's shape, R2):
        // measured 2026-09-06, the deviation from the closed form is close
        // to a FIXED ~0.001 absolute near Nyquist, not a fraction of the
        // (there tiny) expected value -- the boundary leakage a linear,
        // non-circular one-sample shift leaves in a finite window is a fixed
        // number of samples' worth of energy, not scaled by R itself. A bare
        // relative epsilon is unattainable exactly where R's closed form
        // heads to its own zero, same shape as an FFT bin near a spectral
        // null.
        const double tolerance = std::abs(expectedR) * 1.0e-2 + 1.5e-3;
        CHECK(actualR == Catch::Approx(expectedR).margin(tolerance));

        // z is a near-zero vector approaching Nyquist -- its phase (atan2 of
        // two near-zero, float32-rounding-sized parts) is numerically
        // meaningless there even though R3's exact-cancellation floor
        // (contributors * DBL_EPSILON) does not trip on a value this size,
        // so this excludes the same region directly rather than trusting
        // phasePresent to do it.
        if (expectedR > 0.05) {
            CHECK(actualPhase == Catch::Approx(expectedPhase).margin(1e-2));
        }
    }

    // The record's own spot literals, quoted verbatim.
    auto rAt = [&](std::size_t bin) {
        return static_cast<double>(combined->bands[topIndex].phaseAgreement[bin]);
    };
    // Measured 2026-09-06: relative error 1.6e-4 / 4e-5 / 6e-4 / (absolute)
    // 2.9e-6 at these four bins -- epsilon(2e-3) gives >3x headroom over the
    // worst of the three, consistent with the two-term tolerance above.
    REQUIRE(rAt(128) == Catch::Approx(0.9238795325112867).epsilon(2e-3));
    REQUIRE(rAt(256) == Catch::Approx(0.7071067811865476).epsilon(2e-3));
    REQUIRE(rAt(384) == Catch::Approx(0.38268343236508984).epsilon(2e-3));
    REQUIRE(rAt(512) == Catch::Approx(0.0).margin(1e-4));
}
