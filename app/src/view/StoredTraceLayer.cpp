// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See
// docs/specs/2026-08-28-trace-library-and-session.md §4.
#include "view/StoredTraceLayer.h"

#include "trace/TraceLibrary.h"
#include "view/MeasureColours.h"
#include "view/TraceDecimator.h"

#include <algorithm>
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

/// One vertical extent per column that has data. Min AND max, never a single
/// representative sample: a narrow null between two representatives simply
/// disappears, and a null is the thing the engineer is hunting.
void strokeExtents(juce::Graphics& g, const std::vector<ColumnExtent>& extents,
                   const PlotGeometry& geometry, int originY) {
    for (std::size_t c = 0; c < extents.size(); ++c) {
        const auto& extent = extents[c];
        if (!extent.hasData) continue;

        // yForDb is top-down, so the MAXIMUM dB is the SMALLER y.
        const float yTop = geometry.yForDb(static_cast<double>(extent.maxValue))
                           - static_cast<float>(originY);
        const float yBottom = geometry.yForDb(static_cast<double>(extent.minValue))
                              - static_cast<float>(originY);

        // A column whose min and max coincide is a flat span, not an absence.
        // Give it a full pixel or the trace vanishes wherever it is level.
        const float height = std::max(1.0f, yBottom - yTop);
        g.fillRect(static_cast<float>(c), yTop, 1.0f, height);
    }
}

}  // namespace

void StoredTraceLayer::draw(juce::Graphics& g, const rta::trace::TraceLibrary& library,
                            const PlotGeometry& geometry) {
    const bool stale = !primed_ || library.revision() != cachedRevision_
                       || !sameRenderInputs(geometry, cachedGeometry_);
    if (stale) rebuild(library, geometry);

    // A library with nothing visible rasterises to no image at all rather than
    // a transparent one: blitting a full-plot-sized transparent image every
    // frame is real fill-rate spent to draw nothing.
    if (image_.isValid()) g.drawImageAt(image_, originX_, originY_);
}

void StoredTraceLayer::rebuild(const rta::trace::TraceLibrary& library,
                               const PlotGeometry& geometry) {
    cachedRevision_ = library.revision();
    cachedGeometry_ = geometry;
    primed_ = true;
    image_ = juce::Image();

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

        // An entry whose trace has no magnitude is not an error to report from
        // a paint path -- a trace can legitimately carry phase or coherence
        // alone. There is simply no curve to draw for it here.
        const auto magnitude = stored->field(rta::trace::Field::Magnitude);
        const double binHz = stored->binHz();
        if (magnitude.empty() || binHz <= 0.0) continue;

        const auto columns = columnsForBins(geometry, binHz, magnitude.size(), originX_, width);
        const auto extents = decimateToColumns(magnitude, columns, width);
        if (extents.empty()) continue;

        ig.setColour(colourForShade(entry.shadeIndex));
        strokeExtents(ig, extents, geometry, originY_);
        drewAnything = true;
    }

    if (drewAnything) image_ = std::move(canvas);
}

}  // namespace rta::view
