// SPDX-License-Identifier: AGPL-3.0-or-later
//
// These expectations are closed forms, not recorded output.
//
// The design-goal response of IEC 61260 / ANSI S1.11 is
//     |H(f)|^-2 = 1 + [ (f/fm - fm/f) / (G^(1/2b) - G^(-1/2b)) ]^(2N)
// which pins two things exactly: the response is 1 at the mid-band frequency
// and 1/2 -- that is -3.0103 dB -- at both band edges, by construction, for
// every fraction and every order.
//
// And the equivalent noise bandwidth of a Butterworth band-pass of order 2N,
// relative to its -3 dB bandwidth, is (pi/2N) / sin(pi/2N). For N = 3 that is
// 1.0471975..., so a band fed white noise of known density must return exactly
// density * bandwidth * 1.0471975. No reference implementation is involved.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/BandWeights.h"
#include "rta/dsp/OctaveBands.h"

#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using namespace rta::dsp;

namespace {

constexpr double kFilterOrder = 3.0;  // 2N = 6, the class-0 Butterworth order

/// (pi/2N) / sin(pi/2N): noise bandwidth of a Butterworth band-pass relative to
/// its -3 dB bandwidth.
double noiseBandwidthRatio() {
    const double x = std::numbers::pi / (2.0 * kFilterOrder);
    return x / std::sin(x);
}

}  // namespace

TEST_CASE("The design-goal response is unity at centre and half at the edges",
          "[bandweights]") {
    for (const int fraction : {1, 3, 12, 24}) {
        CAPTURE(fraction);
        const OctaveBands bands(fraction, 500.0, 2000.0);
        REQUIRE(!bands.empty());

        for (const auto& band : bands.bands()) {
            CAPTURE(band.index);
            CHECK_THAT(BandWeights::responseAt(band.centre, band),
                       WithinRel(1.0, 1.0e-12));
            // -3.0103 dB, by construction, at both edges.
            CHECK_THAT(BandWeights::responseAt(band.lower, band),
                       WithinRel(0.5, 1.0e-12));
            CHECK_THAT(BandWeights::responseAt(band.upper, band),
                       WithinRel(0.5, 1.0e-12));
        }
    }
}

TEST_CASE("A band fed white noise returns its noise bandwidth", "[bandweights]") {
    constexpr double fs = 48000.0;
    constexpr std::size_t fftSize = 32768;

    const OctaveBands bands(3, 100.0, 10000.0);
    const BandWeights weights(bands, fftSize, fs);
    REQUIRE(weights.size() == bands.size());

    // Flat density of 1.0 per hertz.
    std::vector<float> density(fftSize / 2 + 1, 1.0f);
    std::vector<float> power(weights.size(), 0.0f);
    weights.apply(density, power);

    const double ratio = noiseBandwidthRatio();

    for (std::size_t i = 0; i < weights.size(); ++i) {
        CAPTURE(i, bands[i].centre, weights.band(i).binsSpanned);
        if (weights.band(i).underResolved) continue;

        const double bandwidth = bands[i].upper - bands[i].lower;
        const double expected = bandwidth * ratio;
        // The sum discretises an integral, so the error scales with one bin
        // against the band's width.
        const double tolerance = 0.02 * expected + 2.0 * fs / (double) fftSize;
        CHECK_THAT((double) power[i], WithinAbs(expected, tolerance));
    }
}

TEST_CASE("Under-resolved bands are flagged by how much of them the FFT can see",
          "[bandweights]") {
    constexpr double fs = 48000.0;
    const OctaveBands bands(3, 20.0, 20000.0);

    // 4096 points at 48 kHz: 11.72 Hz bins. The 31.5 Hz band is 7.3 Hz wide, so
    // the FFT cannot resolve it at all; by 200 Hz there is room to spare.
    const BandWeights coarse(bands, 4096, fs);
    for (std::size_t i = 0; i < coarse.size(); ++i) {
        CAPTURE(bands[i].centre, coarse.band(i).binsSpanned);
        const double bandwidth = bands[i].upper - bands[i].lower;
        CHECK_THAT(coarse.band(i).binsSpanned,
                   WithinRel(bandwidth / (fs / 4096.0), 1.0e-12));
        CHECK(coarse.band(i).underResolved == (coarse.band(i).binsSpanned < 3.0));
    }

    // A longer transform must not un-resolve anything the short one resolved.
    const BandWeights fine(bands, 32768, fs);
    for (std::size_t i = 0; i < fine.size(); ++i) {
        CAPTURE(bands[i].centre);
        if (!coarse.band(i).underResolved) CHECK(!fine.band(i).underResolved);
    }
    CHECK(coarse.band(0).underResolved);
    CHECK(!coarse.band(coarse.size() - 1).underResolved);
}

TEST_CASE("Weights are truncated where they stop mattering", "[bandweights]") {
    const OctaveBands bands(3, 100.0, 10000.0);
    const BandWeights weights(bands, 16384, 48000.0);

    std::size_t stored = 0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        const auto row = weights.weights(i);
        REQUIRE(!row.empty());
        stored += row.size();
        // Nothing kept below the truncation floor, and the peak is inside.
        double peak = 0.0;
        for (const float w : row) {
            CHECK(w > 0.0f);
            peak = std::max(peak, (double) w);
        }
        CHECK_THAT(peak, WithinAbs(1.0, 0.02));
    }

    // Far cheaper than a dense matrix, which is the whole point of storing it
    // sparsely.
    const std::size_t dense = weights.size() * (16384 / 2 + 1);
    CHECK(stored < dense / 3);
}

TEST_CASE("apply rejects mismatched spans", "[bandweights]") {
    const OctaveBands bands(3, 100.0, 10000.0);
    const BandWeights weights(bands, 4096, 48000.0);

    std::vector<float> wrongDensity(100, 1.0f);
    std::vector<float> power(weights.size(), 0.0f);
    CHECK_THROWS_AS(weights.apply(wrongDensity, power), std::invalid_argument);

    std::vector<float> density(4096 / 2 + 1, 1.0f);
    std::vector<float> wrongPower(3, 0.0f);
    CHECK_THROWS_AS(weights.apply(density, wrongPower), std::invalid_argument);
}
