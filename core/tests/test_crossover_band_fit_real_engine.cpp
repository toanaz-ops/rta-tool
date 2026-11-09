// SPDX-License-Identifier: AGPL-3.0-or-later
//
// crossoverBandFit against transfer functions stored by the REAL DualFftEngine.
//
// WHY THIS FILE EXISTS. test_crossover_band_fit.cpp builds H_B by hand with an
// assumed sign for "a capture with D applied". A hand-built fixture shares its
// author's assumption with the code under test, so it cannot detect that
// assumption being wrong. test_dualfft_applied_delay_sign.cpp (T0) measured the
// engine instead: a capture stored with applied delay D carries
//     H_stored = H_true * e^{+j 2 pi f D / fs}      (positive D = advance).
// Here the engine itself produces hA and hB under D_A != D_B and the fit must
// still return the physical relative delay.
//
// Closed form. Reference x, HP side lags by L_A samples, LP side by L_B. Then
// H_true,X[k] = e^{-j 2 pi k L_X / N}, H_stored,X = H_true,X e^{+j 2 pi k D_X/N}.
// The fit's tau is "B arrives later than A", i.e. (L_B - L_A)/fs, and the
// constant phase is 0 (polarity +). Without the correction the fit would read
// (L_B - L_A - (D_B - D_A))/fs; with a wrong-signed correction, off by
// 2 (D_B - D_A)/fs.
#include "rta/dsp/CrossoverFit.h"
#include "rta/dsp/DualFftEngine.h"
#include "rta/gen/Synthetic.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <complex>
#include <optional>
#include <vector>

using namespace rta::dsp;
using Catch::Matchers::WithinAbs;

namespace {

constexpr std::size_t kN = 1024;
constexpr double kFs = 48000.0;

/// Stored H = Sxy / Sxx of one engine run: y is x circularly lagged by `lag`
/// (periodic input + rectangular window => exact, as in the T0 file).
std::vector<std::complex<double>> storedTransfer(int lag, int appliedDelay, bool invert) {
    rta::gen::SyntheticPink source(kN, -12.0, 83);
    std::vector<float> x(kN * 32);
    source.render(x);
    std::vector<float> y(x.size());
    for (std::size_t n = 0; n < x.size(); ++n) {
        const float v = x[(n + x.size() - static_cast<std::size_t>(lag)) % x.size()];
        y[n] = invert ? -v : v;
    }
    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN; c.window = WindowType::Rectangular;
    c.fifoDepth = 4096;
    c.referenceDelaySamples = appliedDelay;
    DualFftEngine engine(c);
    engine.process(x, y);

    std::vector<std::complex<double>> h(engine.numBins());
    for (std::size_t k = 0; k < h.size(); ++k) {
        h[k] = engine.crossPsd()[k] / engine.referencePsd()[k];
    }
    return h;
}

std::vector<float> unitCoherence(std::size_t n) { return std::vector<float>(n, 1.0f); }

BandFit fitStored(int lagA, int delayA, int lagB, int delayB, bool invertB) {
    const auto hA = storedTransfer(lagA, delayA, false);
    const auto hB = storedTransfer(lagB, delayB, invertB);
    BandFitOptions o;
    o.centreHz = 3000.0;  // 1500..6000 Hz: ~96 bins of 46.875 Hz
    o.octavesEachSide = 1.0;
    o.tauRangeSeconds = 0.002;  // well inside the 21.3 ms bin-spacing ambiguity
    o.tauGridSeconds = 1.0e-6;
    o.sampleRate = kFs;
    o.appliedDelayDifferenceSamples = static_cast<double>(delayB - delayA);
    return crossoverBandFit(hA, hB, unitCoherence(hA.size()), unitCoherence(hB.size()),
                            kFs / static_cast<double>(kN), o);
}

}  // namespace

TEST_CASE("crossoverBandFit recovers the physical relative delay from engine-stored H with D_A != D_B",
          "[crossover_band_fit][applied_delay]") {
    // L_A = 3, L_B = 8 => B later by 5 samples. D_A = 2, D_B = 11: the stored
    // lags are 1 and -3, so an uncorrected fit would read -4 samples; a
    // wrong-signed correction would read 5 - 2*9 = -13.
    const auto fit = fitStored(3, 2, 8, 11, false);
    REQUIRE(fit.refusal == CrossoverRefusal::None);
    CHECK_THAT(fit.tauSeconds, WithinAbs(5.0 / kFs, 1e-6));
    CHECK_THAT(fit.interceptRadians, WithinAbs(0.0, 1e-3));
}

TEST_CASE("crossoverBandFit keeps polarity and negative D difference with engine-stored H",
          "[crossover_band_fit][applied_delay]") {
    // D_B - D_A = -7. B inverted: constant phase pi, delay still +5 samples.
    const auto fit = fitStored(3, 9, 8, 2, true);
    REQUIRE(fit.refusal == CrossoverRefusal::None);
    CHECK_THAT(fit.tauSeconds, WithinAbs(5.0 / kFs, 1e-6));
    CHECK_THAT(std::abs(fit.interceptRadians), WithinAbs(3.14159265358979, 1e-3));
}
