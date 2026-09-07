// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/eq/FilterSpec.h"

#include <complex>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rta::eq {

/// The allocator's whole problem statement (record Sec.3): spans in, no
/// target, no measured trace -- residualDb is already `measured - target -
/// autoOffset`, computed by whoever calls this (EqAllocator, Task D). All
/// spans share length M (hz.size()) except `placed` (solveGains) and
/// `excluded`'s own region encoding (EqAllocator's own concern, Task D).
struct EqInput {
    std::span<const float> hz;          ///< bin frequency, Hz, ascending
    std::span<const float> residualDb;  ///< r_k = m_k - t_k - c
    std::span<const float> coherence;   ///< gamma^2 in [0,1]; 0 where absent
    std::span<const std::uint8_t> trusted;   ///< non-zero => bin enters the fit
    std::span<const std::uint8_t> excluded;  ///< non-zero => bin declined (Sec.2)
    double sampleRate = 0.0;
    int maxFilters = 6;      ///< N cap, a labelled default (record Sec.12.3)
    double gCapDb = 6.0;     ///< per-filter boost cap, a labelled judgement
    double qMaxBoost = 10.0; ///< fallback Q ceiling, boosts (no T60)
    double qMaxCut = 20.0;   ///< fallback Q ceiling, cuts (no T60)
    std::optional<double> roomT60Sec;  ///< ties Q_max when present (record Sec.3)

    /// DEVIATION FROM THE PLAN'S DECLARED STRUCT, ADDED FOR TASK D: the
    /// plan's "core API this lane builds" section lists EqInput without a
    /// complex-H field, but EqAllocator's G24 gate (classifyDip, Task B)
    /// needs excessPhase()'s own hHalfGrid argument, and |H| is exactly
    /// abs(hHalfGrid) -- so this is the ONE extra field that makes the gate
    /// callable at all, not a second magnitude field. Empty (default) means
    /// "no complex transfer function available"; EqAllocator then places
    /// boosts WITHOUT the G24 gate (labelled fallback, flagged to the
    /// orchestrator alongside EQ-R1..R5) rather than refusing every boost
    /// or silently assuming Boostable. solveGains itself never reads this.
    std::span<const std::complex<double>> hHalfGrid;
};

/// gainsDb.size() == placed.size(); conditionNumber is cond(S^T W S + lambda*I)
/// in the 2-norm (via the same Cholesky factorisation the solve itself uses --
/// see EqGainSolve.cpp for why that is a cheap, honest estimate here and not
/// everywhere).
struct GainSolve {
    std::vector<double> gainsDb;
    double conditionNumber = 1.0;
};

/// Weighted ridge least squares in dB (record Sec.3): for FIXED (fc, Q) per
/// placed filter, linearises each cascade section as g_i * s_i(f),
/// s_i(f) = responseDb({type_i, fc_i, Q_i, 1 dB}, sampleRate, f) (EQ-R5,
/// exact -- Wave 0's designBiquad/responseDb, never re-derived), then solves
///
///     g = argmin sum_k w_k (r_k - sum_i s_i(f_k) g_i)^2 + lambda * ||g||^2
///
/// as (S^T W S + lambda*I) g = S^T W r by Cholesky, in double: N <= 16
/// unknowns, one solve, no iteration, no tolerance, no initial guess.
///
/// Weights w_k = gamma^2_k / f_k on TRUSTED, NON-EXCLUDED bins with f_k > 0,
/// zero elsewhere (record Sec.3: gamma^2 is L6b's bounded trust -- inverse-
/// variance weighting was rejected there for 99:1 dominance, memory/
/// positions-are-not-replicates.md -- 1/f is the log-axis Jacobian).
///
/// lambda is DERIVED, not chosen: lambda = 0.1 * min_i(s_i^T W s_i) /
/// (gCapDb - 0.1) -- the smallest per-filter column norm among `placed`, so
/// shrinkage at the boost cap is exactly 0.1 dB for the filter ridge would
/// shrink MOST (record Sec.3).
///
/// @param placed  the filters already placed (fc, Q, type fixed); gainDb is
///                ignored on input and is what this function solves for.
/// @throws std::invalid_argument if placed is empty, placed.size() > 16, any
///         of hz/residualDb/coherence/trusted/excluded are not the same
///         length as hz, sampleRate <= 0, or gCapDb <= 0.1 (lambda's own
///         denominator would be non-positive).
[[nodiscard]] GainSolve solveGains(std::span<const FilterSpec> placed, const EqInput& input);

}  // namespace rta::eq
