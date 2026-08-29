// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Decision 5 of
// docs/dsp/2026-08-29-display-layer-l5c.md.
//
// This file deliberately does NOT include view/TraceDecimator.h and does not
// reuse ColumnExtent. Phase and magnitude need genuinely different reductions,
// and a shared type is the shortest path back to somebody calling the
// magnitude decimator on wrapped phase -- the exact defect this file exists to
// prevent.
#pragma once

#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace rta::view {

/// Wrap to (-180, 180] -- the degree form of core's own (-pi, pi] convention
/// for `rta::dsp::TransferSnapshot::phaseRadians`. A value that already
/// survived the engine's wrap is a fixed point of this function, so wrapping
/// twice cannot drift at the boundary.
[[nodiscard]] inline float wrapTo180(float deg) noexcept {
    float w = std::fmod(deg + 180.0f, 360.0f);
    if (w <= 0.0f) w += 360.0f;
    return w - 180.0f;
}

/// One pixel column's phase extent, in UNWRAPPED degrees. `hasData` is not
/// redundant with a zero extent: a column no bin lands in must draw nothing,
/// and a phase of 0 degrees drawn where nothing was measured is a flat line
/// through the middle of the pane, which reads as a perfectly aligned system.
struct PhaseColumn {
    float minDeg = 0.0f;
    float maxDeg = 0.0f;
    bool hasData = false;
};

/// A column ready to draw, in wrapped degrees.
///
/// `straddlesWrap` means the extent crosses the +-180 boundary, so the column
/// is the union of [minDeg, +180] and [-180, maxDeg] -- two short pieces at the
/// two edges of the pane. `fullBand` means the UNWRAPPED extent reached a whole
/// turn: phase is rotating faster than one pixel column can resolve, and a
/// full-height band is the honest rendering of that (decision 5).
struct DrawnPhaseColumn {
    float minDeg = 0.0f;
    float maxDeg = 0.0f;
    bool hasData = false;
    bool straddlesWrap = false;
    bool fullBand = false;
    /// Do not draw a connecting line from the previous column to this one.
    bool penLift = false;
};

/// Running unwrap along the bin axis: each bin adjusted by the multiple of 360
/// that brings it within 180 degrees of its predecessor.
///
/// A view-side operation on a LOCAL COPY, permitted precisely because dual-FFT
/// record section 6 put unwrap in the view and out of every accumulator. The
/// stored wrapped values are never modified, so the one-bad-bin fragility this
/// carries corrupts one rebuild of one cached image and no more -- the next
/// revision bump rebuilds from the stored values again.
[[nodiscard]] inline std::vector<float> unwrapAlongBins(std::span<const float> wrappedDeg) {
    std::vector<float> out(wrappedDeg.size());
    if (wrappedDeg.empty()) return out;

    out[0] = wrappedDeg[0];
    for (std::size_t i = 1; i < wrappedDeg.size(); ++i) {
        float delta = wrappedDeg[i] - wrappedDeg[i - 1];
        delta -= 360.0f * std::round(delta / 360.0f);
        out[i] = out[i - 1] + delta;
    }
    return out;
}

/// Unwrap along bins, then take min/max per column ON THE UNWRAPPED VALUES,
/// where an extent is a true extent.
///
/// The two rejected alternatives, so nobody re-derives them: raw wrapped
/// min/max manufactures a ~358 degree span from +179 and -179; one
/// representative bin per column aliases, because at fftSize 32768 the top
/// decade packs tens of bins into a column and a few milliseconds of delay
/// rotates phase through several full turns inside one of them -- a single
/// sample lands anywhere, frame to frame, and the trace shimmers.
[[nodiscard]] inline std::vector<PhaseColumn> decimatePhaseToColumns(
    std::span<const float> wrappedDeg, std::span<const int> columnForBin, int columnCount) {
    if (columnCount <= 0 || wrappedDeg.empty() || wrappedDeg.size() != columnForBin.size()) {
        return {};
    }

    const std::vector<float> unwrapped = unwrapAlongBins(wrappedDeg);
    std::vector<PhaseColumn> out(static_cast<std::size_t>(columnCount));
    for (std::size_t i = 0; i < unwrapped.size(); ++i) {
        const int column = columnForBin[i];
        if (column < 0 || column >= columnCount) continue;  // caller's mapping, not our crash

        auto& c = out[static_cast<std::size_t>(column)];
        const float v = unwrapped[i];
        if (!c.hasData) {
            c.minDeg = v;
            c.maxDeg = v;
            c.hasData = true;
        } else {
            if (v < c.minDeg) c.minDeg = v;
            if (v > c.maxDeg) c.maxDeg = v;
        }
    }
    return out;
}

/// Map unwrapped column extents back into the +-180 pane, and decide where the
/// pen lifts.
///
/// The pen lifts wherever a connecting line would be an artefact rather than
/// part of the curve: at the first drawn column, after a hole, on either side
/// of a band or a straddle, and wherever the drawn midpoints jump by more than
/// 180 degrees.
[[nodiscard]] inline std::vector<DrawnPhaseColumn> wrapForDrawing(
    std::span<const PhaseColumn> columns) {
    std::vector<DrawnPhaseColumn> out(columns.size());

    bool havePrevious = false;
    float previousMid = 0.0f;

    for (std::size_t i = 0; i < columns.size(); ++i) {
        const PhaseColumn& in = columns[i];
        DrawnPhaseColumn& d = out[i];
        if (!in.hasData) {
            havePrevious = false;  // nothing to connect the NEXT column back to
            continue;
        }
        d.hasData = true;

        if ((in.maxDeg - in.minDeg) >= 360.0f) {
            d.fullBand = true;
            d.minDeg = -180.0f;
            d.maxDeg = 180.0f;
            d.penLift = true;
            havePrevious = false;
            continue;
        }

        d.minDeg = wrapTo180(in.minDeg);
        d.maxDeg = wrapTo180(in.maxDeg);
        d.straddlesWrap = d.minDeg > d.maxDeg;

        const float mid = wrapTo180(0.5f * (in.minDeg + in.maxDeg));
        d.penLift = !havePrevious || d.straddlesWrap || std::abs(mid - previousMid) > 180.0f;

        previousMid = mid;
        havePrevious = !d.straddlesWrap;
    }
    return out;
}

}  // namespace rta::view
