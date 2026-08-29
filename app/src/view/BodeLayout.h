// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Decisions 1 and 2 of
// docs/dsp/2026-08-29-display-layer-l5c.md.
#pragma once

#include "view/AxisMetrics.h"
#include "view/PlotGeometry.h"

#include <algorithm>

namespace rta::view {

/// An integer pixel rectangle, in the component's own coordinates.
///
/// Not `juce::Rectangle<int>`, which would drag juce_graphics into a file the
/// guard requires to be framework-free -- and not a float rectangle either: a
/// pane boundary lands on a pixel row or it draws a hairline seam, so integers
/// are the honest type for the thing being computed. The float-valued
/// `PlotGeometry` takes over once the DRAWING starts, where sub-pixel positions
/// are what a stroked path actually wants.
struct PaneRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] constexpr int right() const noexcept { return x + width; }
    [[nodiscard]] constexpr int bottom() const noexcept { return y + height; }
};

/// The three stacked panes of the Bode composite, top to bottom.
struct BodePanes {
    PaneRect ribbon;
    PaneRect magnitude;
    PaneRect phase;
};

/// The shared log-frequency mapping. Decision 1: ONE function computes
/// `left`/`right`/`fLowHz`/`fHighHz`, and every pane's `PlotGeometry` is built
/// from it -- panes with independently computed x mappings that disagree by a
/// pixel would silently destroy the only compensation stacking has.
struct FrequencyAxis {
    float left = 0.0f;
    float right = 0.0f;
    double fLowHz = 20.0;
    double fHighHz = 20000.0;
};

/// Furniture, not data: the ribbon is an axis strip and does not scale with the
/// window (decision 2). Same value the mockup used, kept because 34 px is what
/// reads as a strip rather than a band at six feet -- the mockup's height was
/// never the thing that was wrong, its magnitude PANE was.
inline constexpr int kRibbonHeight = 34;

/// 5 : 3 = 62.5 / 37.5, which keeps the default-size composite essentially what
/// the owner has already seen while replacing the constant that produced it.
/// Magnitude takes the larger share because that is where the 0.1 dB readout
/// rule has to stay resolvable; phase at a fixed +-180 axis needs less
/// resolution per pixel to be read to the nearest few degrees. This is a
/// judgement -- no standard allocates Bode pane heights -- and if a draggable
/// splitter ever ships it moves the weight, not the rule.
inline constexpr int kMagnitudeWeight = 5;
inline constexpr int kPhaseWeight = 3;

/// Split `content` into ribbon / magnitude / phase.
///
/// The frequency label strip is reserved from the BOTTOM before the
/// magnitude-phase split, exactly as the mockup does: it belongs to the
/// bottom-most pane only, and taking it out first is what lets both panes keep
/// an identical plot height rule.
[[nodiscard]] inline BodePanes bodePanes(PaneRect content, int gap) noexcept {
    BodePanes panes;

    const int ribbonHeight = std::min(kRibbonHeight, std::max(0, content.height));
    panes.ribbon = { content.x, content.y, content.width, ribbonHeight };

    int y = content.y + ribbonHeight + gap;
    const int available =
        std::max(0, content.bottom() - kFrequencyLabelHeight - y - gap);

    // Both edges from one accumulated fraction, for the same reason
    // az::ui::splitVertically does it: rounding the two panes independently
    // leaves an unpainted row between them at most heights.
    constexpr int total = kMagnitudeWeight + kPhaseWeight;
    const int magnitudeHeight = available * kMagnitudeWeight / total;
    const int phaseHeight = available - magnitudeHeight;

    panes.magnitude = { content.x, y, content.width, magnitudeHeight };
    y += magnitudeHeight + gap;
    panes.phase = { content.x, y, content.width, phaseHeight };
    return panes;
}

/// The shared x mapping, computed ONCE per composite.
[[nodiscard]] inline FrequencyAxis frequencyAxis(PaneRect content, double fLowHz = 20.0,
                                                 double fHighHz = 20000.0) noexcept {
    FrequencyAxis axis;
    axis.left = static_cast<float>(content.x + kLevelLabelWidth);
    axis.right = static_cast<float>(content.right() - static_cast<int>(kFrequencyLabelHalfWidth));
    // Content narrower than the furniture it has to carry (kLevelLabelWidth
    // + kFrequencyLabelHalfWidth, 66 px) would otherwise yield `right <
    // left` -- an INVERTED axis, the horizontal cousin of the negative-pane-
    // height case `bodePanes` above already guards. `xForHz`'s log ratio
    // divides by `log(fHighHz/fLowHz)`, never by `right - left`, so an
    // inverted axis does not crash there -- it silently flips every x
    // pixel's sense, which is worse than a crash because nothing downstream
    // notices. Clamped to a single point rather than swapped: a swapped
    // axis would still be usable-looking (frequency would just run
    // backwards), and this is the one case where usable-looking is the bug.
    axis.right = std::max(axis.right, axis.left);
    axis.fLowHz = fLowHz;
    axis.fHighHz = fHighHz;
    return axis;
}

/// A pane's full `PlotGeometry`: x from the shared axis, y from the pane.
///
/// `pane.x` and `pane.width` are deliberately unread. A pane that later gains a
/// horizontal inset must NOT get its own frequency mapping -- that is the exact
/// defect decision 1 is defended against, and `test_bode_layout.cpp` asserts
/// this function ignores those two fields.
[[nodiscard]] inline PlotGeometry paneGeometry(const FrequencyAxis& axis, PaneRect pane,
                                               double dbTop, double dbBottom) noexcept {
    PlotGeometry geometry;
    geometry.left = axis.left;
    geometry.right = axis.right;
    geometry.top = static_cast<float>(pane.y);
    geometry.bottom = static_cast<float>(pane.bottom());
    geometry.fLowHz = axis.fLowHz;
    geometry.fHighHz = axis.fHighHz;
    geometry.dbTop = dbTop;
    geometry.dbBottom = dbBottom;
    return geometry;
}

}  // namespace rta::view
