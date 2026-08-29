// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rtatool_view_tests.
//
// TraceStroke's job is one pixel column at a time: given an extent (or a
// phase column) and a per-column trust value, put the right ink in the right
// place at the right opacity. No cache, no library, no revision counter --
// that is StoredTraceLayer's job, tested separately. Everything here is read
// back with juce::Image::BitmapData the same way test_stored_trace_layer.cpp
// does, through the software renderer, with no window and no message loop.
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "view/CoherenceAlpha.h"
#include "view/PhaseDecimator.h"
#include "view/PlotGeometry.h"
#include "view/TraceDecimator.h"
#include "view/TraceStroke.h"

#include <span>
#include <vector>

namespace {

using rta::view::alphaForCoherence;
using rta::view::ColumnExtent;
using rta::view::DrawnPhaseColumn;
using rta::view::PlotGeometry;
using rta::view::strokeMagnitudeExtents;
using rta::view::strokePhaseColumns;

const juce::Colour kBackground = juce::Colours::black;
const juce::Colour kBase = juce::Colours::white;

juce::Image blankImage(int width, int height) {
    juce::Image image(juce::Image::ARGB, width, height, true);
    juce::Graphics g(image);
    g.fillAll(kBackground);
    return image;
}

/// A magnitude geometry where 1 dB is exactly 1 pixel row: dbTop=0 at row 0,
/// dbBottom=-height at the last row. A flat extent at -half the height then
/// lands on an EXACT integer row with no antialiasing at the boundary, which
/// is what lets the pixel-equality assertions below be exact rather than
/// "close to".
PlotGeometry magnitudeGeometry(int width, int height) {
    PlotGeometry geometry;
    geometry.left = 0.0f;
    geometry.right = static_cast<float>(width);
    geometry.top = 0.0f;
    geometry.bottom = static_cast<float>(height);
    geometry.fLowHz = 20.0;
    geometry.fHighHz = 20000.0;
    geometry.dbTop = 0.0;
    geometry.dbBottom = -static_cast<double>(height);
    return geometry;
}

/// A phase geometry where 1 degree is exactly 1 pixel row over a full turn:
/// +180 at row 0, -180 at row 360 (the last valid row is 359). Integer rows
/// for every multiple-of-one-degree input, for the same reason as above.
PlotGeometry phaseGeometry(int width) {
    PlotGeometry geometry;
    geometry.left = 0.0f;
    geometry.right = static_cast<float>(width);
    geometry.top = 0.0f;
    geometry.bottom = 360.0f;
    geometry.fLowHz = 20.0;
    geometry.fHighHz = 20000.0;
    geometry.dbTop = 180.0;
    geometry.dbBottom = -180.0;
    return geometry;
}

}  // namespace

// CATCHES: alpha applied in the wrong direction (dimmer columns drawing
// BRIGHTER), alpha ignored entirely (both columns drawing identically), and
// the floor being treated as zero (a fully untrusted column vanishing).
// record §8's "assert the drawn alpha differs between high- and low-gamma^2
// columns", plus the "still visible" half that pins kUntrustedAlphaFloor.
TEST_CASE("a low-trust column is dimmer than a high-trust one, and still visible",
          "[trace-stroke]") {
    constexpr int kWidth = 2;
    constexpr int kHeight = 10;
    const auto geometry = magnitudeGeometry(kWidth, kHeight);

    // Same colour, same extent (-5 dB, flat) in both columns -- the ONLY
    // difference between them is alpha.
    std::vector<ColumnExtent> extents(2);
    extents[0] = { -5.0f, -5.0f, true };
    extents[1] = { -5.0f, -5.0f, true };

    const std::vector<float> alpha = { alphaForCoherence(1.0f), alphaForCoherence(0.0f) };

    auto image = blankImage(kWidth, kHeight);
    {
        juce::Graphics g(image);
        strokeMagnitudeExtents(g, extents, geometry, 0, alpha, kBase);
    }

    // -5 dB over a 0..-10 axis, 10 px tall, lands exactly on row 5.
    const auto highTrust = image.getPixelAt(0, 5);
    const auto lowTrust = image.getPixelAt(1, 5);

    REQUIRE(highTrust != kBackground);
    CHECK(lowTrust != kBackground);  // the floor, not zero -- still visible
    CHECK(lowTrust.getRed() < highTrust.getRed());
}

// CATCHES: treating a missing alpha span as "coherence zero" (defaulting to
// the floor) instead of "no coherence measured" (opaque) -- exactly the
// distinction TraceStroke.h's contract comment draws. A wrong implementation
// here draws the empty-span column at kUntrustedAlphaFloor, which this test
// would see as strictly dimmer than the full-trust reference.
TEST_CASE("an empty alpha span draws opaque", "[trace-stroke]") {
    constexpr int kWidth = 1;
    constexpr int kHeight = 10;
    const auto geometry = magnitudeGeometry(kWidth, kHeight);

    std::vector<ColumnExtent> extents(1);
    extents[0] = { -5.0f, -5.0f, true };

    auto fullTrust = blankImage(kWidth, kHeight);
    {
        juce::Graphics g(fullTrust);
        const std::vector<float> alpha = { alphaForCoherence(1.0f) };
        strokeMagnitudeExtents(g, extents, geometry, 0, alpha, kBase);
    }

    auto noCoherence = blankImage(kWidth, kHeight);
    {
        juce::Graphics g(noCoherence);
        strokeMagnitudeExtents(g, extents, geometry, 0, std::span<const float>{}, kBase);
    }

    REQUIRE(fullTrust.getPixelAt(0, 5) != kBackground);
    CHECK(noCoherence.getPixelAt(0, 5) == fullTrust.getPixelAt(0, 5));
}

// CATCHES: the whole 358-degree-band defect, one level below where
// PhaseDecimator's own test catches it -- an implementation that draws
// [minDeg, maxDeg] as ONE rect when minDeg > maxDeg would paint the entire
// middle of the pane, which is exactly what this test's centre-row check
// refuses.
TEST_CASE("a straddling phase column paints both edges and not the middle",
          "[trace-stroke]") {
    constexpr int kWidth = 1;
    const auto geometry = phaseGeometry(kWidth);  // 360 px tall, 1 row/degree

    std::vector<DrawnPhaseColumn> columns(1);
    columns[0].hasData = true;
    columns[0].straddlesWrap = true;
    columns[0].minDeg = 179.0f;
    columns[0].maxDeg = -179.0f;

    auto image = blankImage(kWidth, 360);
    {
        juce::Graphics g(image);
        strokePhaseColumns(g, columns, geometry, 0, std::span<const float>{}, kBase);
    }

    // +180 -> row 0, -180 -> row 359: the two edges, one row each, at this
    // geometry's exact 1-degree-per-row scale.
    CHECK(image.getPixelAt(0, 0) != kBackground);    // top edge (near +180)
    CHECK(image.getPixelAt(0, 359) != kBackground);  // bottom edge (near -180)
    CHECK(image.getPixelAt(0, 180) == kBackground);  // vertical centre (0 deg): untouched
}

// CATCHES: fullBand not actually spanning the pane -- an implementation that
// silently clamps a full-band column to the ordinary min/max path (e.g. by
// checking straddlesWrap instead of fullBand) would leave the centre row
// painted but NOT the top/bottom edges, since minDeg/maxDeg is already
// -180/180 for a fullBand column and only the code path differs.
TEST_CASE("a full-band column paints the whole pane height", "[trace-stroke]") {
    constexpr int kWidth = 1;
    const auto geometry = phaseGeometry(kWidth);

    std::vector<DrawnPhaseColumn> columns(1);
    columns[0].hasData = true;
    columns[0].fullBand = true;
    columns[0].minDeg = -180.0f;
    columns[0].maxDeg = 180.0f;

    auto image = blankImage(kWidth, 360);
    {
        juce::Graphics g(image);
        strokePhaseColumns(g, columns, geometry, 0, std::span<const float>{}, kBase);
    }

    CHECK(image.getPixelAt(0, 0) != kBackground);    // top
    CHECK(image.getPixelAt(0, 180) != kBackground);  // centre
    CHECK(image.getPixelAt(0, 359) != kBackground);  // bottom
}

// CATCHES: a loop that ignores `hasData` and draws every column regardless --
// which would pass every OTHER test here, since all of them only supply
// columns that DO have data. Column 0 is checked for ink too, not just
// column 1 for its absence: a strokeMagnitudeExtents that draws NOTHING at
// all would make the "no data" half vacuously true.
TEST_CASE("a column with no data paints nothing", "[trace-stroke]") {
    constexpr int kWidth = 2;
    constexpr int kHeight = 10;
    const auto geometry = magnitudeGeometry(kWidth, kHeight);

    std::vector<ColumnExtent> extents(2);
    extents[0] = { -5.0f, -5.0f, true };
    // extents[1] left default-constructed: hasData == false.

    auto image = blankImage(kWidth, kHeight);
    {
        juce::Graphics g(image);
        strokeMagnitudeExtents(g, extents, geometry, 0, std::span<const float>{}, kBase);
    }

    int inkColumn0 = 0;
    int inkColumn1 = 0;
    for (int y = 0; y < kHeight; ++y) {
        if (image.getPixelAt(0, y) != kBackground) ++inkColumn0;
        if (image.getPixelAt(1, y) != kBackground) ++inkColumn1;
    }

    CHECK(inkColumn0 > 0);
    CHECK(inkColumn1 == 0);
}
