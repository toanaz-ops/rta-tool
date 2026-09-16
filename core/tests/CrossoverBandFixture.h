// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Shared fixtures for the crossoverBandFit cases, split across
// test_crossover_band_fit.cpp (C1-C4, the estimate and its refusals) and
// test_crossover_band_candidates.cpp (C5-C7, what the fit reports about its
// own uncertainty) when the single file passed the 400-line cap.
#pragma once

#include "rta/dsp/CrossoverFit.h"

#include <cmath>
#include <complex>
#include <numbers>
#include <optional>
#include <vector>

namespace rta_test {

// Every fixture function is `inline`: two translation units include this
// header, so without it each would carry its own definition and the two could
// drift apart silently.
using rta::dsp::BandFitOptions;

constexpr double kPi = std::numbers::pi;

inline double wrapToPi(double radians) noexcept { return std::remainder(radians, 2.0 * kPi); }

inline std::optional<std::vector<float>> unitCoherence(std::size_t n) {
    return std::optional<std::vector<float>>{ std::vector<float>(n, 1.0f) };
}

/// C1's fixture, fully declared. binWidthHz is a REQUIRED input (ALIGN-R5), so
/// it is part of the fixture rather than an afterthought: 1.0 Hz bins, window
/// 40-160 Hz inclusive, therefore N = 121 gated bins.
constexpr double kBinHz = 1.0;
constexpr std::size_t kBins = 201;      // 0 .. 200 Hz, so the window sits inside
constexpr std::size_t kWindowBins = 121;  // 40..160 inclusive
constexpr double kCentreHz = 80.0;      // 80/2 = 40 and 80*2 = 160, both exact
constexpr double kGrid = 1.0e-6;

inline BandFitOptions baseOptions() {
    BandFitOptions options;
    options.centreHz = kCentreHz;
    options.octavesEachSide = 1.0;
    options.tauRangeSeconds = 0.020;
    options.tauGridSeconds = kGrid;
    options.maxCycleCandidates = 3;
    options.minimumGatedCoherence = 0.0;
    options.sampleRate = 48000.0;
    return options;
}

/// H_A == 1, H_B = e^{j(phi0 - 2 pi f tau0)}: a pure delay plus a constant,
/// which is exactly the model the fit is built on, so R must come out 1.
inline void pureDelayPair(double tau0, double phi0, std::vector<std::complex<double>>& hA,
                   std::vector<std::complex<double>>& hB, double extraDelaySeconds = 0.0) {
    hA.assign(kBins, std::complex<double>{ 1.0, 0.0 });
    hB.resize(kBins);
    for (std::size_t k = 0; k < kBins; ++k) {
        const double f = static_cast<double>(k) * kBinHz;
        hB[k] = std::polar(1.0, phi0 - 2.0 * kPi * f * (tau0 + extraDelaySeconds));
    }
}

/// 1 - R for a flat window of N bins spaced `delta` Hz, evaluated `dtau` off
/// the true delay. The fit sums over BINS, not a continuum, so the exact form
/// is the Dirichlet kernel |sin(pi N delta dtau) / (N sin(pi delta dtau))|.
inline double dirichletAgreement(std::size_t n, double delta, double dtau) {
    const double x = kPi * delta * dtau;
    if (std::abs(x) < 1e-300) return 1.0;
    return std::abs(std::sin(static_cast<double>(n) * x) / (static_cast<double>(n) * std::sin(x)));
}


}  // namespace rta_test
