// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Decision 3 of
// docs/dsp/2026-08-29-display-layer-l5c.md -- the CONTINUOUS mechanism only.
// Every threshold (blanking, gating, a match verdict) is L5b's, and none may
// appear in this file.
#pragma once

#include "view/TraceDecimator.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace rta::view {

/// Alpha for a completely untrusted column. Zero would make the display
/// silently DELETE data, which is the gate's job to do loudly; much above this
/// and the fade stops reading as a fade at all. A judgement, and the record
/// says so.
inline constexpr float kUntrustedAlphaFloor = 0.25f;

/// Monotone in gamma^2, floored. Linear is the simplest curve satisfying what
/// the record actually fixes -- monotone, floored at 0.25, 1.0 at full
/// coherence. Anything shaped (a power, a knee) would be a threshold in
/// disguise, and thresholds belong to L5b.
///
/// A NaN fails `> 0.0f` and lands on the floor. That is deliberate and not
/// defensive clutter: core clamps and gates its coherence, but a stored trace
/// comes off disk, and a NaN compares false against every bound -- an
/// unguarded comparison would paint an unmeasurable bin at full confidence.
[[nodiscard]] inline float alphaForCoherence(float gammaSquared) noexcept {
    if (!(gammaSquared > 0.0f)) return kUntrustedAlphaFloor;
    const float t = std::min(gammaSquared, 1.0f);
    return kUntrustedAlphaFloor + (1.0f - kUntrustedAlphaFloor) * t;
}

/// Per-column alpha, the column taking the MINIMUM gamma^2 of its bins:
/// trust shown never exceeds trust measured.
///
/// Reuses `decimateToColumns` rather than growing a second reduction -- its
/// `minValue` is exactly the quantity wanted here. (Phase could not reuse it;
/// see PhaseDecimator.h for why that is a real difference and not an
/// inconsistency.) Columns no bin landed in get the floor; whether such a
/// column draws at all is decided by its extent's `hasData`, not here.
[[nodiscard]] inline std::vector<float> columnAlpha(std::span<const float> coherence,
                                                    std::span<const int> columnForBin,
                                                    int columnCount) {
    const auto columns = decimateToColumns(coherence, columnForBin, columnCount);
    if (columns.empty()) return {};

    std::vector<float> out(columns.size(), kUntrustedAlphaFloor);
    for (std::size_t c = 0; c < columns.size(); ++c) {
        if (columns[c].hasData) out[c] = alphaForCoherence(columns[c].minValue);
    }
    return out;
}

}  // namespace rta::view
