// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Record §7, task B4: level alignment over the span the operator is
// CURRENTLY displaying, REW's "Align SPL over a chosen span" -- no fixed
// normalisation band ships (Smaart's 225 Hz-8.8 kHz default is a judgement,
// not a derivation, and prints its own subwoofer failure mode).
#pragma once

#include "rta/dsp/TransferEstimator.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace rta::measure {

/// Sets `trimsOut[i]` so that each position's own coherence-weighted mean
/// level over `[lowHz, highHz]` equals the group's own coherence-weighted
/// mean level over the SAME band, and returns the dB move applied to reach
/// it (`trimsOut[i]_new - trimsOut[i]_old`).
///
/// `trimsOut[i]` is read BEFORE the call as position i's current dB
/// calibration (0.0 for a position never aligned) -- it is added to that
/// position's own `magnitudeDb` before either mean is computed, so calling
/// this twice in a row is a fixed point: after the first call every
/// position's own level already equals the group's, and the group level
/// (a weighted mean of values that are now all equal to it) does not move
/// either, so every second-call move is exactly 0.
///
/// This is DELIBERATELY NOT `rta::dsp::spatialAverage`'s own `u_i` combine
/// weight (record §3): `u_i` multiplies `gamma^2` inside a WEIGHTED MEAN OF
/// LEVELS and can rebalance how much a position's already-fixed reading
/// pulls the group average, but it cannot make one position's OWN reading
/// equal another's -- there is no weight that shifts what `20*log10|H_i|`
/// already says. An additive dB correction is REW's own precedent for this
/// action ("Align SPL"), and it is what this function returns; a caller
/// who ALSO wants this fed into `AverageGroup`'s `u_i` is a separate
/// decision this function does not make.
///
/// The weight at each bin is the position's OWN gated coherence -- `0` (or
/// absent) at a bin excludes it from that position's own mean, exactly as
/// `spatialAverage`'s own weight excludes an ungated position from the
/// group combine (record §3's reasoning, one layer up: an unweighted mean
/// would let a noisy bin drag a position's summary level the same amount as
/// a trusted one).
///
/// A position with zero trust across the WHOLE band (every bin absent or
/// coherence exactly 0) gets no trim -- its entry in the returned vector is
/// `std::nullopt`, and `trimsOut[i]` is left untouched, never becoming a
/// `0/0`. If NO position has ANY trust anywhere in `[lowHz, highHz]` --
/// including a span containing no bin at all, e.g. narrower than
/// `binWidthHz` and unaligned to any bin centre -- every position reports
/// `std::nullopt`, since there is no group level to align anything to.
///
/// `positions.size() != trimsOut.size()` is a caller error; every entry
/// comes back `std::nullopt` and `trimsOut` is untouched.
[[nodiscard]] inline std::vector<std::optional<double>> alignTrims(
    std::span<const rta::dsp::TransferSnapshot> positions, double lowHz, double highHz,
    std::span<double> trimsOut) {
    std::vector<std::optional<double>> moves(positions.size());
    if (positions.size() != trimsOut.size()) return moves;

    // One pass per position: this position's own coherence-weighted
    // sum/weight over the band, with its CURRENT trim already folded into
    // the level -- see the function's own comment on why that is what
    // makes a second call a fixed point.
    std::vector<double> weightedSums(positions.size(), 0.0);
    std::vector<double> weightSums(positions.size(), 0.0);
    double groupWeightedSum = 0.0;
    double groupWeightSum = 0.0;

    for (std::size_t i = 0; i < positions.size(); ++i) {
        const auto& snap = positions[i];
        if (!snap.coherence.has_value() || snap.binWidthHz <= 0.0) continue;
        const auto& coherence = *snap.coherence;
        const std::size_t bins = std::min(coherence.size(), snap.magnitudeDb.size());
        for (std::size_t k = 0; k < bins; ++k) {
            const double hz = static_cast<double>(k) * snap.binWidthHz;
            if (hz < lowHz || hz > highHz) continue;
            const double weight = static_cast<double>(coherence[k]);
            if (!(weight > 0.0)) continue;
            const double levelDb = static_cast<double>(snap.magnitudeDb[k]) + trimsOut[i];
            weightedSums[i] += weight * levelDb;
            weightSums[i] += weight;
        }
        groupWeightedSum += weightedSums[i];
        groupWeightSum += weightSums[i];
    }

    // No position carries any trust anywhere in the band (including a band
    // with no bin in it at all): there is no group level to align to.
    if (!(groupWeightSum > 0.0)) return moves;
    const double groupLevelDb = groupWeightedSum / groupWeightSum;

    for (std::size_t i = 0; i < positions.size(); ++i) {
        if (!(weightSums[i] > 0.0)) continue;  // zero trust: no move, no trim
        const double positionLevelDb = weightedSums[i] / weightSums[i];
        const double move = groupLevelDb - positionLevelDb;
        trimsOut[i] += move;
        moves[i] = move;
    }
    return moves;
}

}  // namespace rta::measure
