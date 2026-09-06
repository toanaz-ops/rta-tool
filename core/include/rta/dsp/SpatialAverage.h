// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/dsp/TransferEstimator.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rta::dsp {

/// §2: dB is Smaart's default -- "a consensus view" (LE v9.1 p.77) -- because
/// a transfer function used to equalise a system answers "what correction
/// serves every listener", not "how loud is the room on average" (SMPTE
/// ST 202 A.3.5, an SPL-averaging clause for a different quantity). Power is
/// kept as the option because it has one documented virtue -- it
/// de-emphasises the nulls of comb filters -- and both modes share one
/// weight (§3).
enum class SpatialMode { Db, Power };

/// Two absences, because the weights can legitimately sum to zero where
/// contributors exist: magnitudeSquaredCoherence() returns exactly 0.0 at a
/// gate-passed bin whenever sxx or syy is non-positive
/// (TransferEstimator.cpp), so every contributor at a bin can carry
/// `W_i = 0`. Record §3.
enum class SpatialAbsence : std::uint8_t { Present, NoContributor, NoWeight };

/// Per-bin bookkeeping the result carries alongside magnitude and phase, so a
/// caller never has to reconstruct "was this bin real" from a 0/0 or a NaN.
struct SpatialBinState {
    SpatialAbsence absence = SpatialAbsence::NoContributor;
    bool phasePresent = false;       ///< R above the R3 floor -- see spatialAverage()
    std::uint16_t contributors = 0;  ///< gate-passed AND u_i > 0 (record §3, R4)
};

/// Weighted spatial combine over N positions against one reference. Every
/// formula here is quoted from docs/dsp/2026-09-06-multichannel-l6b.md §2-§5,
/// not invented in this file.
struct SpatialAverageResult {
    SpatialMode mode = SpatialMode::Db;
    double sampleRate = 0.0;
    double binWidthHz = 0.0;
    std::vector<float> magnitudeDb;        ///< §2's L or L_pow; floored once at kMagnitudeFloorDb
    std::vector<float> phaseRadians;       ///< arg(z), (-pi, pi]; 0.0 (untouched) where absent
    std::vector<float> phaseAgreement;     ///< R = |z|, §2 -- NOT a coherence estimate
    std::vector<float> weightedCoherence;  ///< Sum(u*g2)/Sum(u), §4 -- NOT one either
    std::vector<SpatialBinState> bins;
};

/// Combines `positions` (already gated by makeSnapshot()) into one spatial
/// trace, weighting each position's contribution at each bin by
/// `u[i] * (*positions[i].coherence)[k]` (§3).
///
/// Throws std::invalid_argument if `positions` is empty, `u.size() !=
/// positions.size()`, any `u[i]` is negative or non-finite, or the positions
/// disagree on `binWidthHz`, `sampleRate` or bin count (`h.size()`) -- exact
/// `!=` on the doubles, because in this app every position's engine is built
/// from one shared Config (§5), so a mismatch is a programming error, never
/// an operator situation.
///
/// Returns std::nullopt when every bin's weights sum to zero -- a curve of
/// nothing must not masquerade as a result
/// (memory/a-fixed-defect-returns-through-the-silent-fallback.md).
[[nodiscard]] std::optional<SpatialAverageResult> spatialAverage(
    std::span<const TransferSnapshot> positions, std::span<const double> u,
    SpatialMode mode = SpatialMode::Db);

}  // namespace rta::dsp
