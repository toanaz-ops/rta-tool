// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/eq/DipClassifier.h"
#include "rta/eq/EqGainSolve.h"
#include "rta/eq/FilterSpec.h"

#include <vector>

namespace rta::eq {

/// One placement, its estimated ghost-preview gain, and the G24 verdict it
/// was placed under -- Suggest's chip vocabulary (record Sec.2).
struct Candidate {
    FilterSpec spec;     ///< gainDb is a SINGLE-FILTER estimate (see below)
    double scoreDb = 0.0; ///< |working residual| at the placement bin -- how
                          ///< urgent this candidate is, for ranking
    DipClassification dip;
};

/// Placement (record Sec.3), shared by rankCandidates and autoEq:
///
///   1. AUTO-OFFSET c: the gamma^2/f-weighted mean of residualDb (read here
///      as the PRE-offset curve m_k - t_k, EqAllocator's own job to correct
///      -- see EqGainSolve.h's own doc comment on what its EqInput expects
///      by the time IT sees residualDb) over every trusted, non-excluded
///      bin (the "anchor band": a labelled simplification -- the plan cites
///      an anchor band without pinning its extent further, so this takes it
///      to be the whole trusted region rather than inventing a narrower
///      one). workingResidual[k] = residualDb[k] - c is what placement and
///      the joint solve both read.
///   2. PLACEMENT: the trusted, non-excluded, not-already-covered bin of
///      largest |workingResidual|; its region [fL, fR] is bounded by the
///      flanking extrema of the OPPOSITE sense. Q = fc / (f2 - f1), f1/f2
///      the bins where workingResidual crosses HALF the placement's own
///      extremum value walking outward from fStar (the cookbook's own
///      half-gain bandwidth definition, EqGainSolve.h's linearisation
///      rests on the same identity) -- clamped to [kQMin, qMaxBoost] for a
///      boost, [kQMin, qMaxCut] for a cut (roomT60Sec ties qMaxBoost to
///      2.2*Q/fc = T60 when present, record Sec.3).
///   3. ANTI-COLLINEARITY: no candidate within half a placed filter's own
///      bandwidth (fc/(2*Q)) of that filter's fc.
///   4. G24 GATE (boosts only, record Sec.4.3.7): when EqInput::hHalfGrid
///      is non-empty, a boost candidate is classified via classifyDip; a
///      NotMinimumPhase verdict drops the candidate from PLACEMENT (never
///      re-gated a second time downstream) and the search continues at the
///      next-largest extremum. Cuts are never gated.
///   5. SHELVES ARE NOT PLACED IN THIS PASS: the plan's shelf-edge rule
///      (record Sec.3, REW's edge rule) is deferred -- every placement here
///      is FilterType::Peaking. Flagged as a scope reduction, not silently
///      dropped (open question for the orchestrator alongside EQ-R1..R5).
///
/// @throws std::invalid_argument on fs<=0, mismatched span lengths, or
///         every bin untrusted/excluded (no extremum exists to place on).
[[nodiscard]] std::vector<Candidate> rankCandidates(const EqInput& input, int maxCandidates);

/// Greedy placement to input.maxFilters (or until no trusted extrema
/// remain), THEN one joint solveGains call over the whole set (record Sec.3:
/// "greedy to N, then one solve") -- placement itself never re-solves
/// between candidates. NMP boosts are placed as boosts nowhere (rule 4
/// above); refused placements do not consume an N slot.
///
/// @throws the same conditions as rankCandidates, plus whatever solveGains
///         itself throws (e.g. an out-of-domain shelf -- moot while this
///         pass places peaking only, kept for when shelves land).
[[nodiscard]] std::vector<FilterSpec> autoEq(const EqInput& input);

/// The floor of the Q clamp range -- a labelled judgement (the plan gives
/// qMaxBoost/qMaxCut explicitly but no Q_min; a filter narrower than this
/// is wider than any half-depth crossing this allocator would ever measure
/// on a smooth residual, so it exists only to keep the clamp well-formed).
inline constexpr double kQMin = 0.3;

}  // namespace rta::eq
