// SPDX-License-Identifier: AGPL-3.0-or-later
// PR #45 fix round 3 (owner decision, 2026-09-27, docs/dsp/2026-09-06-l7-
// alignment-wizard.md Sec.13 Q3): the UNKNOWN-inversion rendering -- two
// labelled candidate lines, distinct strokes, a +-180 frame wrap, and an
// "INVERSION UNKNOWN" corner chip. Tests view/CrossoverSurfaceRenderer.
// {h,cpp} directly against a bare CrossoverSurface: `setAskedTopology`
// alone is enough (CrossoverSurface.cpp's own implementation) -- `target_`/
// `marks_` never need `setSources`/`setWindow` to be valid, so no
// VirtualTrace/library fixture is needed here at all.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "measure/CrossoverTopology.h"
#include "view/CrossoverSurface.h"
#include "view/CrossoverSurfaceRenderer.h"
#include "view/MeasureColours.h"

#include <az_ui/az_ui.h>

#include <juce_graphics/juce_graphics.h>

using rta::measure::CrossoverFamily;
using rta::measure::ProcessorInversion;
using rta::view::CrossoverSurface;

namespace {

/// Pixels in `image` within `region` whose colour matches `wanted` exactly.
/// Used to detect ONE specific drawn element (the chip's `borderline` text)
/// without picking up unrelated ink -- a grid hairline or an axis label --
/// that merely happens to fall inside the same rectangle. Same
/// `getPixelAt`-per-pixel shape test_stored_trace_layer.cpp's own
/// `inkPixels` uses.
int countPixelsOfColour(const juce::Image& image, juce::Rectangle<int> region, juce::Colour wanted) {
    int count = 0;
    for (int y = region.getY(); y < region.getBottom(); ++y) {
        for (int x = region.getX(); x < region.getRight(); ++x) {
            if (image.getPixelAt(x, y) == wanted) ++count;
        }
    }
    return count;
}

/// Pixels anywhere in `image` that are NOT exactly `background` -- "any
/// ink", used by the M6 render-and-diff test below.
int countNonBackgroundPixels(const juce::Image& image, juce::Colour background) {
    int count = 0;
    for (int y = 0; y < image.getHeight(); ++y) {
        for (int x = 0; x < image.getWidth(); ++x) {
            if (image.getPixelAt(x, y) != background) ++count;
        }
    }
    return count;
}

juce::Image renderPhase(const CrossoverSurface& surface, juce::Rectangle<int> area) {
    juce::Image image(juce::Image::ARGB, area.getWidth(), area.getHeight(), true);
    juce::Graphics g(image);
    g.fillAll(az::ui::background);
    rta::view::paintCrossoverPhase(g, area, surface);
    return image;
}

CrossoverSurface makeAsked(rta::measure::Topology topology, ProcessorInversion inversion) {
    CrossoverSurface surface;
    surface.setAskedTopology(topology, inversion);
    return surface;
}

}  // namespace

TEST_CASE("targetLineRowsDeg wraps a +/-180 candidate to both frame edges, inset",
         "[crossover_surface_renderer]") {
    // Mutant (owner's round-3 list): the +/-180 wrap not drawn at -180 --
    // returning only the top row, or the un-inset 180 itself, fails either
    // check below directly, with no rendering needed.
    const auto rows = rta::view::targetLineRowsDeg(180.0, 3.0);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0] == Catch::Approx(177.0));
    CHECK(rows[1] == Catch::Approx(-177.0));

    // The symmetric edge, checked the same way (wrapToPiHalfOpen itself
    // never emits -180, but this function does not lean on that forever --
    // see its own header comment).
    const auto negativeEdge = rta::view::targetLineRowsDeg(-180.0, 3.0);
    REQUIRE(negativeEdge.size() == 2);
}

TEST_CASE("targetLineRowsDeg leaves a mid-plot candidate as a single, un-inset row",
         "[crossover_surface_renderer]") {
    const auto rows = rta::view::targetLineRowsDeg(90.0, 3.0);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == Catch::Approx(90.0));
}

TEST_CASE("targetLineLabel follows the actual line, not a fixed 0/180 position",
         "[crossover_surface_renderer]") {
    // Mutant (owner's round-3 list): the labels swapped (NOT INVERTED on the
    // pi line). For an LR-2 pair the PRIMARY candidate itself is 180 deg
    // (CrossoverTopology.cpp: order=2 -> raw=180), so a hardcoded "0 is NOT
    // INVERTED" reads backwards there -- these two pairs of checks are the
    // LR-2 shape and the LR-4 shape (primary at 0), the label following the
    // line either way.
    CHECK(rta::view::targetLineLabel(180.0, /*isPrimaryCandidate=*/true) == "180 deg  NOT INVERTED");
    CHECK(rta::view::targetLineLabel(0.0, /*isPrimaryCandidate=*/false) == "0 deg  INVERTED");
    CHECK(rta::view::targetLineLabel(0.0, /*isPrimaryCandidate=*/true) == "0 deg  NOT INVERTED");
    CHECK(rta::view::targetLineLabel(180.0, /*isPrimaryCandidate=*/false) == "180 deg  INVERTED");
}

TEST_CASE("the INVERSION UNKNOWN chip paints only when the target is ambiguous",
         "[crossover_surface_renderer]") {
    constexpr int kWidth = 800;
    constexpr int kHeight = 400;
    const juce::Rectangle<int> area(0, 0, kWidth, kHeight);
    const auto geometry = rta::view::crossoverPhaseGeometry(area);
    // The chip's own bounds (CrossoverSurfaceRenderer.cpp's drawAmbiguousChip):
    // (geometry.left + 4, geometry.top + 4, 232, 18).
    const juce::Rectangle<int> chipRegion(static_cast<int>(geometry.left) + 4,
                                          static_cast<int>(geometry.top) + 4, 232, 18);

    const auto ambiguousImage =
        renderPhase(makeAsked({ CrossoverFamily::LinkwitzRiley, 4 }, ProcessorInversion::Unknown), area);
    CHECK(countPixelsOfColour(ambiguousImage, chipRegion, rta::view::borderline) > 0);

    // Mutant (owner's round-3 list): the chip shown when NOT ambiguous --
    // this check is exactly what would fail (count > 0 for a non-ambiguous
    // surface, when it must be 0).
    const auto singleImage =
        renderPhase(makeAsked({ CrossoverFamily::LinkwitzRiley, 4 }, ProcessorInversion::No), area);
    CHECK(countPixelsOfColour(singleImage, chipRegion, rta::view::borderline) == 0);
}

TEST_CASE("the ambiguous render paints substantially more ink than the single-line render",
         "[crossover_surface_renderer]") {
    // Verifier mutant M6: `if (surface.targetAmbiguous())` -> `if (false)`.
    // Under that mutant an ambiguous surface falls into the SAME single-
    // dotted-line branch the non-ambiguous case below already takes, so the
    // two renders become pixel-identical and this test's own diff collapses
    // to 0 -- well under the minimum derived next.
    //
    // For LR-4, Unknown: the primary candidate (0 deg) is the SAME row the
    // non-ambiguous (LR-4, No) render also draws, dotted, in the same
    // colour -- those pixels are identical between the two images and
    // cancel out of the diff entirely. What is left is ONLY the alternative
    // candidate (180 deg, wrapped to two rows at +-(180-inset) since it
    // sits exactly on the frame) plus its label and the chip. Each wrapped
    // row alone is a SOLID `drawSolidRow` fill, `(geometry.right -
    // geometry.left)` pixels wide by 1px tall, of `rta::view::target`
    // colour -- and there are two such rows. Requiring only HALF of ONE
    // row's width as the minimum diff is a generous margin for
    // anti-aliasing, and the label/chip glyphs only ADD ink on top, never
    // remove it.
    constexpr int kWidth = 800;
    constexpr int kHeight = 400;
    const juce::Rectangle<int> area(0, 0, kWidth, kHeight);
    const auto geometry = rta::view::crossoverPhaseGeometry(area);
    const int expectedMinimumDiff = static_cast<int>((geometry.right - geometry.left) / 2.0f);
    REQUIRE(expectedMinimumDiff > 0);

    const auto ambiguousImage =
        renderPhase(makeAsked({ CrossoverFamily::LinkwitzRiley, 4 }, ProcessorInversion::Unknown), area);
    const auto singleImage =
        renderPhase(makeAsked({ CrossoverFamily::LinkwitzRiley, 4 }, ProcessorInversion::No), area);

    const int ambiguousInk = countNonBackgroundPixels(ambiguousImage, az::ui::background);
    const int singleInk = countNonBackgroundPixels(singleImage, az::ui::background);
    CHECK(ambiguousInk - singleInk >= expectedMinimumDiff);
}
