// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/dsp/ExcessPhase.h"

#include <cstddef>
#include <span>

namespace rta::eq {

/// Boostable: the dip lives in H_min, a biquad boost can invert it.
/// NotMinimumPhase: "phase problem, not EQ problem" (record Sec.4.4) -- the
/// excess-phase swing across the dip's own region exceeds what its own
/// depth's minimum-phase hypothesis predicts; routed to V2, never boosted.
/// Untrusted: too little trusted data to say either (xp.valid == false).
enum class DipVerdict { Boostable, NotMinimumPhase, Untrusted };

/// D, S, S* and their margin, carried for DISPLAY -- this is the
/// `Polarity.h:103-110` lesson (memory/... none yet, but the same shape):
/// a verdict is reported, never silently re-gated a second time downstream.
struct DipClassification {
    DipVerdict verdict = DipVerdict::Untrusted;
    double depthDb = 0.0;      ///< D: peak-to-notch depth, lower flank (conservative)
    double swingRad = 0.0;     ///< S: max(phi_x) - min(phi_x) over [fL, fR]
    double thresholdRad = 0.0; ///< S* = 2*asin(r_D), the depth-derived boundary
};

/// The G24 verdict (record Sec.4.3.6): under the two-path model, a dip's own
/// depth D fixes r_D = (10^(D/20)-1)/(10^(D/20)+1); the minimum-phase
/// hypothesis predicts excess-phase swing S=0, the non-minimum-phase one
/// S=4*asin(r_D). The boundary S*=2*asin(r_D) is the maximum-margin split
/// between the two closed-form outcomes -- computed from THIS dip's own
/// depth, never a fixed angle (memory/
/// a-threshold-read-off-a-grid-is-that-grids-floor.md: a fixed 90 deg
/// threshold misses every NMP dip shallower than ~7 dB, record Sec.4.4).
///
/// @param xp          the excessPhase() result over the SAME grid as hz and
///                     residualDb; xp.valid == false yields Untrusted
///                     immediately (excessPhase's own "too few trusted bins"
///                     refusal is the ONLY trust signal this function has --
///                     region-level trust filtering, if a caller wants it,
///                     happens before fL/fStar/fR are chosen, e.g. by
///                     EqAllocator's placement only ever proposing candidates
///                     from trusted extrema).
/// @param hz           frequency per bin, Hz -- carried for a caller's own
///                     reporting; not read by this function's own math (the
///                     bin indices below are what index every array here).
/// @param fL, fStar, fR bin indices bounding the dip's region: fL <= fStar
///                     <= fR, fR < hz.size(). fStar is the notch bin,
///                     fL/fR the flanking extrema of the opposite sense
///                     (EqAllocator's own placement rule, Task D).
/// @param residualDb   the ALLOCATOR's residual r_k = m_k - t_k - c (record
///                     Sec.3), NOT the raw measured magnitude -- D is read
///                     off this curve because that is the curve a boost
///                     candidate is placed against.
/// @param isBoost      accepted for call-site documentation only: this
///                     function's own D/S/S*/verdict computation does NOT
///                     depend on it (B3: "the classifier does not itself
///                     drop the candidate"). "Boosts only" gating (record
///                     Sec.4.3.7 -- a wrong cut costs level, a wrong boost
///                     costs headroom and adds ringing) is EqAllocator's
///                     job (Task D): it refuses PLACEMENT only when
///                     isBoost && verdict == NotMinimumPhase, and always
///                     reports the same honest verdict/margin on a cut.
/// @throws std::invalid_argument if hz/residualDb/xp's own arrays are not
///         all the same length, or fL <= fStar <= fR <= (that length - 1)
///         does not hold.
[[nodiscard]] DipClassification
classifyDip(const dsp::ExcessPhaseResult& xp, std::span<const float> hz,
            std::size_t fL, std::size_t fStar, std::size_t fR,
            std::span<const float> residualDb, bool isBoost);

}  // namespace rta::eq
