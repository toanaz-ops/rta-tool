// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// L7-EQ task E, reconciliation EQ-R2 (docs/plans/2026-09-07-L7-eq-impl-plan.md;
// decision record docs/dsp/2026-09-06-l7-auto-eq.md sec.4.1). The core
// allocator owns NO threshold -- it takes a caller-supplied `trusted` mask,
// record sec.4.1 verbatim. This file is that caller, and the one place the
// app's interim coherence floor is written down.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rta::measure {

/// The interim data-quality floor, a LABELLED judgement (EQ-R2, open question
/// 2 in the plan): a bin enters the EQ fit only where gamma^2 >= this. It is
/// a plain floor and nothing else -- explicitly NOT the ISO-2969 X-curve
/// tolerance table, which is about named-preset conformance (record sec.5),
/// not about having a coherence gate at all.
///
/// The physical reason a floor exists: the H1 variance of record sec.8,
/// sigma ~ (20/ln10)*sqrt((1-gamma^2)/(2*n_d*gamma^2)), diverges as gamma^2
/// falls, so below some gamma^2 the residual being fitted is mostly the
/// estimator's own noise. 0.7 is where this project puts that line until L5b
/// owns the threshold as a product decision; when it does, the app swaps the
/// SOURCE of theta and nothing in core/ changes.
inline constexpr float kEqTrustFloor = 0.7f;

/// `coherence` is a TransferSnapshot's coherence vector, or empty.
///
/// Empty (or any length other than `bins`) yields an all-untrusted mask:
/// below the coherence gate a TransferSnapshot carries no coherence vector at
/// all (rta/dsp/TransferEstimator.h), and no evidence is a refusal, never a
/// pass. That asymmetry is the whole point of the gate -- a mask that
/// defaulted to trusted would let the allocator boost into exactly the data
/// the gate withheld.
[[nodiscard]] inline std::vector<std::uint8_t>
buildTrustMask(std::span<const float> coherence, std::size_t bins,
               float trustFloor = kEqTrustFloor) {
    std::vector<std::uint8_t> trusted(bins, static_cast<std::uint8_t>(0));
    if (coherence.size() != bins) return trusted;
    for (std::size_t k = 0; k < bins; ++k) {
        // `>=`: the floor itself is trusted. A strict `>` would make the
        // documented constant unreachable and the boundary untestable.
        trusted[k] = (coherence[k] >= trustFloor) ? static_cast<std::uint8_t>(1)
                                                  : static_cast<std::uint8_t>(0);
    }
    return trusted;
}

}  // namespace rta::measure
