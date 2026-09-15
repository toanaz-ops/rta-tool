// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Shared fixtures for the L7-EQ session tests. Extracted 2026-09-15 when the
// round-2 verify cases pushed test_eq_session.cpp past the project's 400-line
// hard cap: the alternative was duplicating sixty lines of grid and bump
// arithmetic into the second file, and two copies of a fixture drift.
//
// Everything here is `inline` because two translation units include it.
#pragma once

#include "measure/EqSession.h"

#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace eqfixture {

inline constexpr double kFs = 48000.0;

/// A log-spaced measurement grid. Ascending, strictly positive, below
/// Nyquist -- the only three things EqInput asks of `hz`.
[[nodiscard]] inline std::vector<float> logGrid(std::size_t bins, double lowHz, double highHz) {
    std::vector<float> hz(bins);
    const double step = std::log10(highHz / lowHz) / static_cast<double>(bins - 1);
    for (std::size_t k = 0; k < bins; ++k) {
        hz[k] = static_cast<float>(lowHz * std::pow(10.0, step * static_cast<double>(k)));
    }
    return hz;
}

/// A real fixed-engine half-grid: DC..Nyquist, `bins` = 2^k + 1, bin k at
/// exactly k * (fs/2)/(bins-1). Used where a bin index has to be a closed
/// form rather than a lookup.
[[nodiscard]] inline std::vector<float> linearHalfGrid(std::size_t bins) {
    std::vector<float> hz(bins);
    const double binWidth = (kFs / 2.0) / static_cast<double>(bins - 1);
    for (std::size_t k = 0; k < bins; ++k) {
        hz[k] = static_cast<float>(static_cast<double>(k) * binWidth);
    }
    return hz;
}

/// One Gaussian-in-log bump: a smooth, single-extremum deviation, so greedy
/// placement has exactly one obvious first move and the half-gain crossings
/// the Q rule walks out to exist on both flanks.
[[nodiscard]] inline std::vector<float> bumpDb(std::span<const float> hz, double centreHz,
                                               double heightDb, double widthOctaves) {
    std::vector<float> m(hz.size(), 0.0f);
    for (std::size_t k = 0; k < hz.size(); ++k) {
        const double octaves = std::log2(static_cast<double>(hz[k]) / centreHz);
        const double z = octaves / widthOctaves;
        m[k] = static_cast<float>(heightDb * std::exp(-0.5 * z * z));
    }
    return m;
}

struct Fixture {
    std::vector<float> hz;
    std::vector<float> measuredDb;
    std::vector<float> targetDb;
    std::vector<float> coherence;
};

[[nodiscard]] inline Fixture makeFixture(float coherenceValue = 0.95f) {
    Fixture f;
    f.hz = logGrid(192, 20.0, 20000.0);
    f.measuredDb = bumpDb(f.hz, 1000.0, 8.0, 0.5);
    f.targetDb.assign(f.hz.size(), 0.0f);
    f.coherence.assign(f.hz.size(), coherenceValue);
    return f;
}

/// The same 8 dB bump, sampled on a linear half-grid of `bins` points. DC is
/// left untrusted: f = 0 has no place in a log-weighted fit, and EqInput's own
/// weighting divides by f.
[[nodiscard]] inline Fixture makeLinearFixture(std::size_t bins) {
    Fixture f;
    f.hz = linearHalfGrid(bins);
    f.measuredDb = bumpDb(f.hz, 1000.0, 8.0, 0.5);
    f.targetDb.assign(f.hz.size(), 0.0f);
    f.coherence.assign(f.hz.size(), 0.95f);
    f.coherence[0] = 0.0f;
    return f;
}

inline void load(rta::measure::EqSession& session, const Fixture& f) {
    session.setMeasurement(f.hz, f.measuredDb, f.targetDb, f.coherence, {}, kFs);
}

}  // namespace eqfixture
