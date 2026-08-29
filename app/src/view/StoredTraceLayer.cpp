// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See
// docs/specs/2026-08-28-trace-library-and-session.md §4.
#include "view/StoredTraceLayer.h"

#include "trace/TraceLibrary.h"
#include "view/CoherenceAlpha.h"
#include "view/MeasureColours.h"
#include "view/PhaseDecimator.h"
#include "view/TraceDecimator.h"
#include "view/TraceStroke.h"

#include <cassert>
#include <cmath>
#include <vector>

namespace rta::view {

namespace {

/// How many visually distinct shades the stored-trace tone can carry before it
/// repeats. SODIUM RACK spends its one accent on the LIVE trace (see
/// `MeasureColours.h`), so recalled captures are separated along brightness
/// rather than hue -- which is exactly what the library's `shadeIndex` field
/// is named for. Four is where the steps below stop being separable against
/// the graphite background; past that the cycle wraps and the trace list, not
/// the plot, is what tells two captures apart. That ceiling is deliberate: a
/// fifth invented colour would either poach the accent or leave the palette.
constexpr int kShadeCount = 4;

/// Multiplied brightness per shade step. Not a linear ramp: perceived
/// lightness compresses at the dim end, so equal multiplicative steps read as
/// roughly equal separation where equal subtractive ones would not.
constexpr float kShadeBrightness[kShadeCount] = { 1.00f, 0.82f, 0.66f, 0.53f };

[[nodiscard]] juce::Colour colourForShade(int shadeIndex) {
    // A negative index is not a caller error worth refusing a draw over -- the
    // trace still exists and still has to appear. Fold it into range instead.
    const int folded = ((shadeIndex % kShadeCount) + kShadeCount) % kShadeCount;
    return storedTrace.withMultipliedBrightness(kShadeBrightness[folded]);
}

/// Only the fields that change what gets rasterised. `PlotGeometry` has no
/// `operator==` and should not grow one for this: the axis limits and the
/// pixel rectangle are what the cache depends on, and a member added later for
/// some unrelated purpose must not silently start invalidating the image.
[[nodiscard]] bool sameRenderInputs(const PlotGeometry& a, const PlotGeometry& b) noexcept {
    return a.left == b.left && a.right == b.right && a.top == b.top && a.bottom == b.bottom
           && a.fLowHz == b.fLowHz && a.fHighHz == b.fHighHz && a.dbTop == b.dbTop
           && a.dbBottom == b.dbBottom;
}

/// Which image column each FFT bin lands in, using the plot's OWN log mapping
/// rather than a second one derived here. Two mappings that disagree put the
/// stored traces a pixel off the live bars, and nobody can see the cause.
///
/// A bin outside the plotted range gets -1, which `decimateToColumns` skips.
/// The x is clamped as a double before the narrowing cast because bin 0 is DC:
/// `xForHz(0)` is -inf, and casting that to int is undefined behaviour, not a
/// large negative number.
[[nodiscard]] std::vector<int> columnsForBins(const PlotGeometry& geometry, double binHz,
                                              std::size_t bins, int originX, int columnCount) {
    std::vector<int> columns(bins, -1);
    for (std::size_t i = 0; i < bins; ++i) {
        const double hz = static_cast<double>(i) * binHz;
        if (hz <= 0.0) continue;

        const double column = std::floor(static_cast<double>(geometry.xForHz(hz)))
                              - static_cast<double>(originX);
        if (column >= 0.0 && column < static_cast<double>(columnCount)) {
            columns[i] = static_cast<int>(column);
        }
    }
    return columns;
}

}  // namespace

StoredTraceLayer::StoredTraceLayer(rta::trace::Field field) : field_(field) {}

void StoredTraceLayer::draw(juce::Graphics& g, const rta::trace::TraceLibrary& library,
                            const PlotGeometry& geometry) {
    // Every term of the key, in cost order. `!hasCached_` also covers the
    // first-ever call.
    const bool stale = !hasCached_ || library.generation() != cachedGeneration_
                       || library.revision() != cachedRevision_
                       || !sameRenderInputs(geometry, cachedGeometry_);
    if (stale) rebuild(library, geometry);

    // A library with nothing visible rasterises to no image at all rather than
    // a transparent one: blitting a full-plot-sized transparent image every
    // frame is real fill-rate spent to draw nothing.
    if (image_.isValid()) g.drawImageAt(image_, originX_, originY_);
}

void StoredTraceLayer::forget() noexcept {
    image_ = juce::Image();
    hasCached_ = false;
}

void StoredTraceLayer::rebuild(const rta::trace::TraceLibrary& library,
                               const PlotGeometry& geometry) {
    // The whole key is written here, in one place, before any early return
    // below can skip part of it and leave the cache describing an image that
    // was never drawn.
    hasCached_ = true;
    cachedGeneration_ = library.generation();
    cachedRevision_ = library.revision();
    cachedGeometry_ = geometry;
    image_ = juce::Image();

    // Counted HERE, not at the top of `draw`: the number a test reads has to
    // mean "the raster work happened", and every early return below is still a
    // rebuild -- it re-derives the origin, re-decides there is nothing to draw,
    // and rewrites the whole key. A zero-width plot and an all-hidden library
    // are cached results, not skipped ones.
    ++rebuildCount_;

    originX_ = static_cast<int>(std::floor(geometry.left));
    originY_ = static_cast<int>(std::floor(geometry.top));
    const int width = static_cast<int>(std::ceil(geometry.right)) - originX_;
    const int height = static_cast<int>(std::ceil(geometry.bottom)) - originY_;
    if (width <= 0 || height <= 0) return;

    // Nothing to draw is answered before an image is allocated, so the common
    // "no session loaded" case costs one loop over a short vector.
    bool anyVisible = false;
    for (const auto& entry : library.entries()) {
        if (entry.visible) {
            anyVisible = true;
            break;
        }
    }
    if (!anyVisible) return;

    juce::Image canvas(juce::Image::ARGB, width, height, true);
    juce::Graphics ig(canvas);
    bool drewAnything = false;

    for (const auto& entry : library.entries()) {
        if (!entry.visible) continue;

        // Not named `trace`: that is the amber LIVE-trace colour token from
        // MeasureColours.h, and shadowing it here is exactly the confusion
        // this layer exists to keep out of the plot.
        const auto* stored = library.trace(entry.traceId);
        if (stored == nullptr) continue;

        // An entry whose trace has no data for THIS instance's field is not an
        // error to report from a paint path -- a trace can legitimately carry
        // phase or coherence alone, or magnitude alone. There is simply no
        // curve to draw for it here.
        const auto values = stored->field(field_);
        const double binHz = stored->binHz();
        if (values.empty() || binHz <= 0.0) continue;

        const auto columns = columnsForBins(geometry, binHz, values.size(), originX_, width);

        // Both fields' alpha comes from the trace's OWN coherence, when it has
        // one -- an empty span otherwise, which strokeMagnitudeExtents and
        // strokePhaseColumns both read as "no coherence measured", not "zero
        // trust" (TraceStroke.h's contract comment).
        const auto coherence = stored->field(rta::trace::Field::Coherence);
        // `coherence` and `columns` can only disagree in length if a future
        // change removes Trace::setCoherence's own size check against
        // magnitude_ -- which is what currently makes the two always equal
        // whenever coherence is non-empty. columnAlpha() would silently
        // return {} (drawing opaque) if that ever broke; this assert turns
        // that into a loud failure in a debug build instead of a trace that
        // quietly stops fading.
        assert(coherence.empty() || coherence.size() == columns.size());
        const auto alpha = coherence.empty() ? std::vector<float>{}
                                             : columnAlpha(coherence, columns, width);

        const auto base = colourForShade(entry.shadeIndex);

        if (field_ == rta::trace::Field::Phase) {
            const auto phaseColumns = decimatePhaseToColumns(values, columns, width);
            if (phaseColumns.empty()) continue;
            // bridgeGaps is NOT applied to phase: it interpolates between two
            // extents, and interpolating ACROSS A WRAP invents a sweep through
            // the whole pane that nothing measured. Magnitude's gaps are a
            // continuous curve sampled too coarsely; a phase straddle is a
            // discontinuity, and joining across it would draw the exact
            // 358-degree artefact wrapForDrawing exists to refuse.
            const auto drawn = wrapForDrawing(phaseColumns);
            strokePhaseColumns(ig, drawn, geometry, originY_, alpha, base);
        } else {
            // bridgeGaps AFTER decimation, not before: it operates on one
            // extent per pixel column, which is exactly what a dotted-scatter
            // low end needs joined -- bridging per-bin would be a different,
            // much bigger change to what "the bins" even are.
            const auto extents = bridgeGaps(decimateToColumns(values, columns, width));
            if (extents.empty()) continue;
            strokeMagnitudeExtents(ig, extents, geometry, originY_, alpha, base);
        }
        drewAnything = true;
    }

    if (drewAnything) image_ = std::move(canvas);
}

}  // namespace rta::view
