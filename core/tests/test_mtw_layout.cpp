// SPDX-License-Identifier: AGPL-3.0-or-later
//
// MtwLayout is pure arithmetic: no FFT, no signal, so every expectation here
// is a closed form from docs/dsp/2026-09-05-mtw-l3.md §2-§3 and the plan's
// corrected §3 table (conflicts C1-C3), never "whatever the code printed".

#include "rta/dsp/AverageCount.h"
#include "rta/dsp/MtwLayout.h"
#include "rta/dsp/Window.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <stdexcept>
#include <vector>

using namespace rta::dsp;

namespace {
MtwConfig defaults() { return MtwConfig{}; }
}  // namespace

TEST_CASE("the MTW band table doubles the FFT size once per octave downward", "[mtw][layout]") {
    const auto bands = mtwBands(defaults());
    REQUIRE(bands.size() == 7);

    const std::vector<std::size_t> expectedFftSizes{65536, 32768, 16384, 8192, 4096, 2048, 1024};
    for (std::size_t i = 0; i < bands.size(); ++i) {
        CAPTURE(i);
        REQUIRE(bands[i].fftSize == expectedFftSizes[i]);
        REQUIRE(bands[i].hopSize == bands[i].fftSize / 4);
    }
    REQUIRE(bands[0].windowSeconds == Catch::Approx(65536.0 / 48000.0).epsilon(1e-12));
    REQUIRE(bands[6].windowSeconds == Catch::Approx(0.02133333333333333).epsilon(1e-9));
}

TEST_CASE("every band owns bins 128..255 except the top and the bottom", "[mtw][layout]") {
    const auto bands = mtwBands(defaults());
    for (std::size_t i = 1; i < 6; ++i) {
        CAPTURE(i);
        REQUIRE(bands[i].firstBin == 128);
        REQUIRE(bands[i].lastBin == 255);
    }
    REQUIRE(bands[0].firstBin == 0);
    REQUIRE(bands[0].lastBin == 255);
    REQUIRE(bands[6].firstBin == 128);
    REQUIRE(bands[6].lastBin == 512);  // topFftSize/2, Nyquist included

    // The general form, at other topFftSize values too -- conflict C1's
    // resolution: firstBin == topFftSize/8 for every band but the bottom.
    for (const std::size_t n0 : {std::size_t{512}, std::size_t{1024}, std::size_t{2048}}) {
        MtwConfig cfg;
        cfg.topFftSize = n0;
        const auto b = mtwBands(cfg);
        for (std::size_t i = 1; i < b.size(); ++i) {
            CAPTURE(n0, i);
            REQUIRE(b[i].firstBin == n0 / 8);
        }
    }
}

TEST_CASE("the stitched frequency vector is strictly increasing and 1281 points long",
          "[mtw][layout]") {
    const auto cfg = defaults();
    REQUIRE(mtwPointCount(cfg) == 1281);
    const auto freq = mtwFrequencies(cfg);
    REQUIRE(freq.size() == 1281);
    REQUIRE(freq[0] == 0.0);
    for (std::size_t i = 1; i < freq.size(); ++i) {
        CAPTURE(i);
        REQUIRE(freq[i] > freq[i - 1]);
    }
    REQUIRE(freq[1280] == Catch::Approx(24000.0).epsilon(1e-9));
}

TEST_CASE("each stitched point's frequency is its owning band's bin frequency",
          "[mtw][layout]") {
    const auto cfg = defaults();
    const auto bands = mtwBands(cfg);
    const auto freq = mtwFrequencies(cfg);

    for (const auto& band : bands) {
        for (std::size_t bin = band.firstBin; bin <= band.lastBin; ++bin) {
            const std::size_t index = band.firstIndex + (bin - band.firstBin);
            const double expected =
                static_cast<double>(bin) * cfg.sampleRate / static_cast<double>(band.fftSize);
            CAPTURE(index, bin, band.fftSize);
            REQUIRE(freq[index] == Catch::Approx(expected).epsilon(1e-9));
        }
    }

    // Spot values that must appear verbatim (record §3's corrected table).
    REQUIRE(freq[255] == Catch::Approx(255.0 * 48000.0 / 65536.0).epsilon(1e-9));
    REQUIRE(freq[256] == Catch::Approx(187.5).epsilon(1e-9));
    REQUIRE(freq[384] == Catch::Approx(375.0).epsilon(1e-9));
    REQUIRE(freq[512] == Catch::Approx(750.0).epsilon(1e-9));
    REQUIRE(freq[640] == Catch::Approx(1500.0).epsilon(1e-9));
    REQUIRE(freq[768] == Catch::Approx(3000.0).epsilon(1e-9));
    REQUIRE(freq[896] == Catch::Approx(6000.0).epsilon(1e-9));
}

TEST_CASE("the owned boundaries are exactly fs / (8 * 2^k)", "[mtw][layout]") {
    const auto bands = mtwBands(defaults());
    const std::vector<double> expected{0.0, 187.5, 375.0, 750.0, 1500.0, 3000.0, 6000.0};
    for (std::size_t i = 0; i < bands.size(); ++i) {
        CAPTURE(i);
        REQUIRE(bands[i].lowerEdgeHz == Catch::Approx(expected[i]).epsilon(1e-12));
    }
    for (std::size_t i = 1; i < bands.size(); ++i) {
        CAPTURE(i);
        const double fromBin = static_cast<double>(bands[i].firstBin) * 48000.0 /
                                static_cast<double>(bands[i].fftSize);
        REQUIRE(bands[i].lowerEdgeHz == Catch::Approx(fromBin).epsilon(1e-9));
    }
}

TEST_CASE("point count follows the closed form when N0 or K changes", "[mtw][layout]") {
    auto check = [](std::size_t n0, std::size_t k, std::size_t expected) {
        MtwConfig cfg;
        cfg.topFftSize = n0;
        cfg.octaveCount = k;
        CAPTURE(n0, k);
        REQUIRE(mtwPointCount(cfg) == expected);
        const auto freq = mtwFrequencies(cfg);
        REQUIRE(freq.size() == expected);
        for (std::size_t i = 1; i < freq.size(); ++i) {
            REQUIRE(freq[i] > freq[i - 1]);
        }
    };
    check(1024, 7, 1409);
    check(512, 6, 641);
    check(512, 3, 449);
    check(2048, 6, 2561);
}

TEST_CASE("the layout refuses a table it cannot express", "[mtw][layout]") {
    auto throws = [](MtwConfig cfg) {
        CHECK_THROWS_AS(validate(cfg), std::invalid_argument);
    };
    auto ok = [](MtwConfig cfg) { CHECK_NOTHROW(validate(cfg)); };

    { MtwConfig c; c.topFftSize = 1023; throws(c); }
    { MtwConfig c; c.topFftSize = 256; throws(c); }
    { MtwConfig c; c.octaveCount = 0; throws(c); }
    { MtwConfig c; c.octaveCount = 11; throws(c); }
    { MtwConfig c; c.octaveCount = 10; ok(c); }
    { MtwConfig c; c.sampleRate = 0.0; throws(c); }
    { MtwConfig c; c.sampleRate = -1.0; throws(c); }
    { MtwConfig c; c.timeConstantFrames = 0.0; throws(c); }
    { MtwConfig c; c.timeConstantFrames = -1.0; throws(c); }
    { MtwConfig c; c.minimumEffectiveAverages = 0.5; throws(c); }
    { MtwConfig c; c.fifoDepth = 0; throws(c); }
    { MtwConfig c; c.fifoDepth = 33; throws(c); }
    { MtwConfig c; c.fifoDepth = 32; ok(c); }
}

TEST_CASE("one alpha and one depth in every band, and seconds only as a report",
          "[mtw][layout]") {
    for (const double tcf : {4.0, 16.0, 64.0}) {
        for (const std::size_t n0 : {std::size_t{512}, std::size_t{1024}, std::size_t{2048}}) {
            MtwConfig cfg;
            cfg.topFftSize = n0;
            cfg.timeConstantFrames = tcf;
            CAPTURE(tcf, n0);

            const double a0 = mtwAlpha(cfg, 0);
            for (std::size_t k = 1; k < cfg.octaveCount + 1; ++k) {
                REQUIRE(mtwAlpha(cfg, k) == a0);  // bit-identical, not Approx
            }
            REQUIRE(a0 == Catch::Approx(1.0 - std::exp(-1.0 / tcf)).epsilon(1e-9));
        }
    }
    // The literal from record §5 / plan C3 point 1, at the shipping defaults.
    REQUIRE(mtwAlpha(defaults(), 0) == Catch::Approx(0.060586937).margin(1e-9));

    const auto cfg = defaults();
    const auto bands = mtwBands(cfg);
    const std::vector<double> expectedSeconds{5.4613333, 2.7306667, 1.3653333, 0.6826667,
                                               0.3413333, 0.1706667, 0.0853333};
    for (std::size_t i = 0; i < bands.size(); ++i) {
        CAPTURE(i);
        const double reported = mtwIntegrationSeconds(cfg, i);
        // The plan's table is rounded to 7 decimal places, so the comparison
        // is an ABSOLUTE margin against that rounding, not a relative one.
        REQUIRE(reported == Catch::Approx(expectedSeconds[i]).margin(1e-7));
        REQUIRE(reported == Catch::Approx(static_cast<double>(cfg.fifoDepth) *
                                           static_cast<double>(bands[i].hopSize) / cfg.sampleRate)
                                .epsilon(1e-12));
        REQUIRE(bands[i].integrationSeconds == Catch::Approx(reported).epsilon(1e-12));
        if (i > 0) {
            // Consecutive entries differ by exactly a factor of two -- the
            // visible consequence of frames being uniform across an octave
            // table.
            REQUIRE(expectedSeconds[i - 1] / expectedSeconds[i] == Catch::Approx(2.0).epsilon(1e-6));
        }
    }
}

TEST_CASE("the default depth clears the coherence gate in every band, and uniform seconds does not",
          "[mtw][layout][averaging]") {
    const auto cfg = defaults();
    const auto bands = mtwBands(cfg);

    // --- The positive half, Fifo. ---
    std::vector<double> fifoNeff;
    for (const auto& band : bands) {
        Window w(cfg.window, band.fftSize);
        const double neff =
            fifoEffectiveAverages(w.coefficients(), band.hopSize, std::min<std::size_t>(16, 16));
        fifoNeff.push_back(neff);
        CAPTURE(band.fftSize);
        REQUIRE(neff == Catch::Approx(8.5866271).epsilon(1e-6));
        REQUIRE(neff == Catch::Approx(fifoNeff.front()).epsilon(1e-9));
        REQUIRE(neff > cfg.minimumEffectiveAverages);
    }

    // The fill on the way there, once -- band-independent, so once is honest.
    {
        Window w(cfg.window, bands[0].fftSize);
        REQUIRE(fifoEffectiveAverages(w.coefficients(), bands[0].hopSize, 14) ==
                Catch::Approx(7.5488).epsilon(1e-4));
        REQUIRE(fifoEffectiveAverages(w.coefficients(), bands[0].hopSize, 15) ==
                Catch::Approx(8.0677).epsilon(1e-4));
        REQUIRE(fifoEffectiveAverages(w.coefficients(), bands[0].hopSize, 16) ==
                Catch::Approx(8.5866).epsilon(1e-4));
        REQUIRE(fifoEffectiveAverages(w.coefficients(), bands[0].hopSize, 32) ==
                Catch::Approx(16.8955).epsilon(1e-4));
        REQUIRE(fifoEffectiveAverages(w.coefficients(), bands[0].hopSize, 8) ==
                Catch::Approx(4.4393).epsilon(1e-4));
        REQUIRE(fifoEffectiveAverages(w.coefficients(), bands[0].hopSize, 8) <
                cfg.minimumEffectiveAverages);
    }

    // --- The positive half, Exponential. ---
    {
        Window w(cfg.window, bands[0].fftSize);
        const double alpha = mtwAlpha(cfg, 0);
        const double neffInf =
            exponentialEffectiveAverages(w.coefficients(), bands[0].hopSize, alpha, 10000);
        REQUIRE(neffInf == Catch::Approx(17.1123296).epsilon(1e-4));
        for (std::size_t i = 1; i < bands.size(); ++i) {
            Window wi(cfg.window, bands[i].fftSize);
            REQUIRE(exponentialEffectiveAverages(wi.coefficients(), bands[i].hopSize, alpha,
                                                  10000) == Catch::Approx(neffInf).epsilon(1e-4));
        }
        REQUIRE(exponentialEffectiveAverages(w.coefficients(), bands[0].hopSize, alpha, 26) ==
                Catch::Approx(7.5204).epsilon(1e-4));
        REQUIRE(exponentialEffectiveAverages(w.coefficients(), bands[0].hopSize, alpha, 27) ==
                Catch::Approx(8.0321).epsilon(1e-4));
    }

    // --- D is the same in every band. ---
    for (const std::size_t n0 : {std::size_t{512}, std::size_t{1024}, std::size_t{2048}}) {
        MtwConfig cfg2;
        cfg2.topFftSize = n0;
        const auto b2 = mtwBands(cfg2);
        for (const auto& band : b2) {
            CAPTURE(n0, band.fftSize);
            Window w(cfg2.window, band.fftSize);
            const double c1 = overlapCorrelation(w.coefficients(), band.hopSize);
            const double c2 = overlapCorrelation(w.coefficients(), 2 * band.hopSize);
            const double c3 = overlapCorrelation(w.coefficients(), 3 * band.hopSize);
            const double c4 = overlapCorrelation(w.coefficients(), 4 * band.hopSize);
            // Plan literals are rounded to 9 decimal places -- absolute margin.
            REQUIRE(c1 == Catch::Approx(0.659154943).margin(1e-8));
            REQUIRE(c2 == Catch::Approx(0.166666667).margin(1e-8));
            REQUIRE(c3 == Catch::Approx(0.007511724).margin(1e-8));
            REQUIRE(c4 == 0.0);
            const double d = 1.0 + 2.0 * (c1 * c1 + c2 * c2 + c3 * c3);
            REQUIRE(d == Catch::Approx(1.9246389).epsilon(1e-6));
        }
    }

    // --- The negative half: uniform SECONDS, and why Config cannot express
    // it. MtwConfig has no per-band tau, so this is asserted directly on the
    // real AverageCount function with a hand-built per-band alpha -- never
    // through MtwEngine, which does not exist yet at this station.
    {
        const std::vector<double> tauFrames{1.46484, 2.92969, 5.85938, 11.71875,
                                             23.4375, 46.875, 93.75};
        const std::vector<double> expectedNeff{2.061, 3.554, 6.584, 12.665,
                                                24.839, 49.193, 97.902};
        for (std::size_t i = 0; i < bands.size(); ++i) {
            const double alpha = 1.0 - std::exp(-1.0 / tauFrames[i]);
            Window w(cfg.window, bands[i].fftSize);
            const double neff =
                exponentialEffectiveAverages(w.coefficients(), bands[i].hopSize, alpha, 10000);
            CAPTURE(i);
            REQUIRE(neff == Catch::Approx(expectedNeff[i]).epsilon(1e-3));
        }
        REQUIRE(expectedNeff.front() < cfg.minimumEffectiveAverages);
        REQUIRE(expectedNeff.back() > 90.0);
    }
}
