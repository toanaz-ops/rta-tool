// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/SpatialAverage.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace rta::dsp {

namespace {

/// Exact `!=` on the doubles is deliberate (header comment): in this app
/// every position's engine is constructed from one shared Config, so a
/// difference here is a programming error, not an operator situation that
/// deserves a tolerance.
void validateInputs(std::span<const TransferSnapshot> positions, std::span<const double> u) {
    if (positions.empty()) {
        throw std::invalid_argument("spatialAverage: positions must not be empty");
    }
    if (u.size() != positions.size()) {
        throw std::invalid_argument("spatialAverage: u.size() must equal positions.size()");
    }
    for (const double weight : u) {
        if (!std::isfinite(weight) || weight < 0.0) {
            throw std::invalid_argument("spatialAverage: every u[i] must be finite and >= 0");
        }
    }
    const auto& first = positions.front();
    for (const auto& position : positions) {
        if (position.binWidthHz != first.binWidthHz || position.sampleRate != first.sampleRate ||
            position.h.size() != first.h.size()) {
            throw std::invalid_argument(
                "spatialAverage: positions disagree on binWidthHz, sampleRate or length");
        }
    }
}

}  // namespace

SpatialAverageResult spatialAverageBins(std::span<const TransferSnapshot> positions,
                                         std::span<const double> u, SpatialMode mode) {
    validateInputs(positions, u);

    const std::size_t bins = positions.front().h.size();
    SpatialAverageResult result;
    result.mode = mode;
    result.sampleRate = positions.front().sampleRate;
    result.binWidthHz = positions.front().binWidthHz;
    result.magnitudeDb.assign(bins, 0.0f);
    result.phaseRadians.assign(bins, 0.0f);
    result.phaseAgreement.assign(bins, 0.0f);
    result.weightedCoherence.assign(bins, 0.0f);
    result.bins.assign(bins, SpatialBinState{});

    for (std::size_t k = 0; k < bins; ++k) {
        double sumW = 0.0;            // Sum W_i over contributors with W_i != 0
        double sumU = 0.0;            // Sum u_i over ALL contributors (R4: u_i > 0 required)
        double sumWeightedGamma2 = 0.0;  // Sum u_i*gamma2_i, weightedCoherence's numerator (§4)
        double sumMagnitudeTerm = 0.0;   // dB: W_i*20log10|H_i|; Power: W_i*|H_i|^2
        std::complex<double> sumZ{0.0, 0.0};
        std::uint16_t contributors = 0;

        for (std::size_t i = 0; i < positions.size(); ++i) {
            const auto& position = positions[i];
            // R4: an ungated snapshot, OR a muted member (u_i == 0), is
            // excluded EVERYWHERE and never raises the contributor count --
            // an all-muted group must report NoContributor, never NoWeight.
            if (!position.coherence.has_value() || !(u[i] > 0.0)) {
                continue;
            }
            ++contributors;

            const double gamma2 = static_cast<double>((*position.coherence)[k]);
            const double weight = u[i] * gamma2;
            sumU += u[i];
            sumWeightedGamma2 += weight;

            // Rule 4 (plan task A1): magnitudeSquaredCoherence() returns 0.0
            // under exactly the conditions that make estimateH1/H2/Hv return
            // {0,0}, so W_i == 0.0 already implies |H_i| == 0 here. Skip
            // explicitly anyway, so a future estimator can never reintroduce
            // a log10(0) or a 0/0 through this function.
            if (weight == 0.0) {
                continue;
            }
            sumW += weight;

            const std::complex<double> h = position.h[k];
            const double magnitude = std::abs(h);
            if (mode == SpatialMode::Db) {
                sumMagnitudeTerm += weight * 20.0 * std::log10(magnitude);
            } else {
                sumMagnitudeTerm += weight * magnitude * magnitude;
            }
            sumZ += weight * (h / magnitude);
        }

        result.bins[k].contributors = contributors;
        // §4: present even where the magnitude is absent -- a bin whose
        // every contributing gamma^2 is 0 is a MEASURED 0, not a missing
        // value, so this is computed independently of the NoWeight branch
        // below (its denominator is Sum(u), not Sum(W)).
        result.weightedCoherence[k] =
            sumU > 0.0 ? static_cast<float>(sumWeightedGamma2 / sumU) : 0.0f;

        if (sumW == 0.0) {
            result.bins[k].absence =
                contributors == 0 ? SpatialAbsence::NoContributor : SpatialAbsence::NoWeight;
            // magnitudeDb, phaseRadians and phaseAgreement stay at their
            // default 0.0f -- UNTOUCHED, not a computed zero (record §3).
            continue;
        }

        result.bins[k].absence = SpatialAbsence::Present;

        const double combined = (mode == SpatialMode::Db)
                                     ? sumMagnitudeTerm / sumW
                                     : 10.0 * std::log10(sumMagnitudeTerm / sumW);
        // Floor the RESULT once, here, never the inputs (rule 3): averaging
        // already-floored magnitudeDb values would bias silent bins toward
        // -120 dB, so kMagnitudeFloorDb appears in exactly this one place.
        result.magnitudeDb[k] =
            std::max(static_cast<float>(combined), TransferSnapshot::kMagnitudeFloorDb);

        const std::complex<double> z = sumZ / sumW;
        const double r = std::abs(z);
        result.phaseRadians[k] = static_cast<float>(std::arg(z));
        result.phaseAgreement[k] = static_cast<float>(r);
        // R3: the rounding floor of a sum of `contributors` unit vectors,
        // derived rather than read off a grid
        // (memory/a-threshold-read-off-a-grid-is-that-grids-floor.md).
        result.bins[k].phasePresent =
            r > static_cast<double>(contributors) * std::numeric_limits<double>::epsilon();
    }

    return result;
}

std::optional<SpatialAverageResult> spatialAverage(std::span<const TransferSnapshot> positions,
                                                    std::span<const double> u, SpatialMode mode) {
    SpatialAverageResult result = spatialAverageBins(positions, u, mode);
    // The record's rule applied exactly once, here: nullopt iff every bin's
    // weights summed to zero -- a curve of nothing must not masquerade as a
    // result (memory/a-fixed-defect-returns-through-the-silent-fallback.md).
    // spatialAverageBins() itself never makes this judgement, so callers that
    // need the real per-bin state of an all-absent slice (spatialAverageMtw,
    // one band at a time) can bypass it.
    const bool anyPresent = std::any_of(result.bins.begin(), result.bins.end(),
                                         [](const SpatialBinState& bin) {
                                             return bin.absence == SpatialAbsence::Present;
                                         });
    if (!anyPresent) {
        return std::nullopt;
    }
    return result;
}

}  // namespace rta::dsp
