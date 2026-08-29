// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Decision 3 of
// docs/dsp/2026-08-29-display-layer-l5c.md -- the CONTINUOUS mechanism only.
// Every threshold (blanking, gating, a match verdict) is L5b's, and none may
// appear in this file.
#pragma once

#include "view/TraceDecimator.h"

#include <algorithm>
#include <cmath>
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
/// inconsistency.)
///
/// Columns no bin landed in are BRIDGED across interior gaps -- record §5a,
/// added after a bridged magnitude/ribbon column-by-column comparison showed
/// a barcode: below roughly 2 kHz an FFT has fewer bins than pixel columns,
/// `bridgeGaps` already fills those holes for the magnitude extent on the
/// argument that a spectrum is continuous and its bins are samples of it, and
/// leaving coherence unbridged meant a bridged trace alternated near-opaque
/// (a real bin) and floor-dim (no bin, defaulting to "untrusted") column by
/// column -- painting "not sampled here" as "measured and found
/// untrustworthy", exactly the false assertion the floor exists to refuse.
/// Reusing `bridgeGaps` on a degenerate (min == max) `ColumnExtent` per
/// column is what applies its own leading/trailing-stays-empty rule here for
/// free: a column with no bin on ONE side (nothing to interpolate from)
/// keeps the floor, so "a column with no bins reports no trust" still holds
/// at the ends of the axis -- it no longer holds in the middle, which is the
/// trade §5a records.
///
/// NaN is sanitised BEFORE the reduction, and that ordering is the whole point.
/// `decimateToColumns` cannot see a NaN: its min accumulation asks
/// `v < extent.minValue`, which is false for NaN, so a NaN arriving AFTER a
/// good bin is silently dropped and the column reports the good bin's trust.
/// The answer would then depend on bin order -- {NaN, 1.0} floors, {1.0, NaN}
/// paints FULL CONFIDENCE -- and the second is precisely the "unmeasurable bin
/// drawn as trustworthy" this file exists to refuse. Sanitising afterwards
/// cannot work either: by then the NaN is gone.
///
/// NaN maps to 0 rather than being filtered out, so the bin stays COUNTED: a
/// column whose bins are all unmeasurable must read as untrusted, not as a
/// column nothing landed in.
///
/// Three short vectors and one `bridgeGaps` pass -- more than the single copy
/// this function used to cost, but still bounded by column count and still
/// paid only when a cached layer is rebuilt (a library edit or a geometry
/// change), never per frame. The extra allocation buys the same invariant
/// `bridgeGaps` already buys magnitude, at a price nothing per-frame measures.
[[nodiscard]] inline std::vector<float> columnAlpha(std::span<const float> coherence,
                                                    std::span<const int> columnForBin,
                                                    int columnCount) {
    std::vector<float> measurable;
    measurable.reserve(coherence.size());
    for (const float v : coherence) {
        measurable.push_back(std::isnan(v) ? 0.0f : v);
    }

    const auto columns = decimateToColumns(measurable, columnForBin, columnCount);

    // Each column's own alpha, as a degenerate (min == max) extent, so
    // `bridgeGaps`' interior-gap interpolation and leading/trailing-stays-
    // empty rule both apply to TRUST exactly as they already apply to
    // magnitude's dB extent -- see this function's own comment for why that
    // is the correct reuse rather than a second gap-filling implementation.
    std::vector<ColumnExtent> alphaExtents(columns.size());
    for (std::size_t c = 0; c < columns.size(); ++c) {
        if (columns[c].hasData) {
            const float a = alphaForCoherence(columns[c].minValue);
            alphaExtents[c] = { a, a, true };
        }
    }
    const auto bridged = bridgeGaps(std::move(alphaExtents));

    std::vector<float> out(columns.size(), kUntrustedAlphaFloor);
    for (std::size_t c = 0; c < bridged.size(); ++c) {
        if (bridged[c].hasData) out[c] = bridged[c].minValue;
    }
    return out;
}

}  // namespace rta::view
