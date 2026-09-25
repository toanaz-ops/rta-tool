// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE, no Qt, no audio-device API:
// enforced by the measure_has_no_framework_deps ctest.
// Lane L6a task W2-D (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md §4, §11).
#pragma once

#include "view/Readouts.h"

#include "rta/meter/Block.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rta::view {

/// The SPL strip's own pixel rectangle -- see `BodeLayout.h`'s `PaneRect` for
/// the same reasoning: integers, because a pane boundary lands on a pixel row
/// or draws a hairline seam, never a `juce::Rectangle`.
struct SplStripRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] constexpr int right() const noexcept { return x + width; }
    [[nodiscard]] constexpr int bottom() const noexcept { return y + height; }
};

/// NUMBERS IN, POSITIONS OUT (record §4, D2): the x axis maps
/// `[oldestBlockIndex, newestBlockIndex]` linearly across the rect -- a block
/// index is already a clock, so unlike the RTA plot's frequency axis this one
/// has no reason to be logarithmic. The y axis maps `[dbBottom, dbTop]`
/// linearly, top-down, clamped -- the same convention `PlotGeometry::yForDb`
/// uses, for the same reason (an unclamped floor draws off the pane).
struct SplStripGeometry {
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;

    std::uint64_t oldestBlockIndex = 0;
    std::uint64_t newestBlockIndex = 0;
    double dbTop = 140.0;
    double dbBottom = 0.0;

    [[nodiscard]] float xForBlockIndex(std::uint64_t blockIndex) const noexcept {
        if (newestBlockIndex <= oldestBlockIndex) return left;
        const double span = static_cast<double>(newestBlockIndex - oldestBlockIndex);
        const double t = static_cast<double>(blockIndex - oldestBlockIndex) / span;
        return left + static_cast<float>(t) * (right - left);
    }

    [[nodiscard]] float yForDb(double db) const noexcept {
        const double clamped = std::clamp(db, dbBottom, dbTop);
        const double t = (dbTop != dbBottom) ? (clamped - dbTop) / (dbBottom - dbTop) : 0.0;
        return top + static_cast<float>(t) * (bottom - top);
    }
};

/// Builds the geometry from a pixel rect and the ring's own span -- the one
/// place a `SplHistory`'s `oldestBlockIndex()`/`newestBlockIndex()` become an
/// axis, so two draws of the same history never disagree on where a block
/// lands.
[[nodiscard]] inline SplStripGeometry splStripGeometry(SplStripRect content,
                                                       std::uint64_t oldestBlockIndex,
                                                       std::uint64_t newestBlockIndex,
                                                       double dbTop, double dbBottom) noexcept {
    SplStripGeometry geometry;
    geometry.left = static_cast<float>(content.x);
    geometry.right = static_cast<float>(content.right());
    geometry.top = static_cast<float>(content.y);
    geometry.bottom = static_cast<float>(content.bottom());
    geometry.oldestBlockIndex = oldestBlockIndex;
    geometry.newestBlockIndex = newestBlockIndex;
    geometry.dbTop = dbTop;
    geometry.dbBottom = dbBottom;
    return geometry;
}

/// One block's position -- POSITION ONLY, no colour and no JUCE type (D2, and
/// `docs/dsp/2026-08-29-display-layer-l5c.md` §7 item 2, the same rule
/// `SplHistory` itself follows). Every block in `blocks` gets a point: this
/// model does no decimation of its own -- that, and any colour, is entirely
/// the draw layer's job, one layer up (record §4).
struct SplStripPoint {
    float x = 0.0f;
    float y = 0.0f;
    std::uint64_t blockIndex = 0;
};

[[nodiscard]] inline std::vector<SplStripPoint> splStripPoints(
    const SplStripGeometry& geometry, std::span<const rta::meter::Block> blocks,
    double referenceOffsetDb) noexcept {
    std::vector<SplStripPoint> points;
    points.reserve(blocks.size());
    for (const auto& block : blocks) {
        if (block.blockSamples == 0) continue;  // no divisor -- nothing to plot
        const double leqDb = 10.0 * std::log10(block.sumSquares /
                                              static_cast<double>(block.blockSamples)) +
                             referenceOffsetDb;
        points.push_back({ geometry.xForBlockIndex(block.blockIndex),
                           geometry.yForDb(leqDb), block.blockIndex });
    }
    return points;
}

/// D3: the SPL strip's own two readouts, through the THREE formatters that
/// exist (`Readouts.h:72,79,87`) -- no fourth is introduced. A frequency axis
/// has no place on this pane, so `formatHz` is not reached here; that is a
/// property of what this pane shows, not an omission.
[[nodiscard]] inline std::string splCurrentLevelLabel(double levelDb) {
    return rta::view::formatTrim(levelDb);  ///< dB, one decimal (CLAUDE.md)
}

[[nodiscard]] inline std::string splBufferFillLabel(double fill01) {
    return rta::view::formatAgreement(fill01);  ///< 0..1, two decimals (CLAUDE.md)
}

}  // namespace rta::view
