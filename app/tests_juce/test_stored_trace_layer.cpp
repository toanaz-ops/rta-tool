// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rtatool_view_tests.
//
// StoredTraceLayer's two claims, made falsifiable:
//   1. the right pixels end up on the image, and
//   2. the image is NOT rebuilt when nothing it depends on moved.
//
// Claim 2 has no visual consequence -- a layer that re-rasterises every frame
// draws exactly the same picture as one that blits a cache -- so it is read
// through `rebuildCount()`, which exists for this and says so in its own
// comment. Claim 1 is read by counting pixels that differ from a known
// background, never by comparing against a golden image: a golden would lock
// in the current shade table and antialiasing and fail on every unrelated
// palette tweak, which teaches the next session to delete the test.
//
// No window, no desktop peer, no message loop: everything below rasterises
// into a juce::Image through the software renderer, the same path
// tools/snapshot.cpp uses headlessly. See app/tests_juce/CMakeLists.txt.
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "trace/Trace.h"
#include "trace/TraceLibrary.h"
#include "view/PlotGeometry.h"
#include "view/StoredTraceLayer.h"

#include <string>
#include <utility>
#include <vector>

namespace {

using rta::trace::CaptureMeta;
using rta::trace::Trace;
using rta::trace::TraceLibrary;
using rta::view::PlotGeometry;
using rta::view::StoredTraceLayer;

constexpr int kFftSize = 1024;
constexpr double kSampleRate = 48000.0;
constexpr int kPlotWidth = 200;
constexpr int kPlotHeight = 100;
constexpr double kDbTop = 0.0;
constexpr double kDbBottom = -90.0;

/// Opaque, and nothing the palette contains: `storedTrace` is a mid grey
/// (az::ui::dim), so "differs from this" cannot accidentally mean "is a dark
/// shade of the trace colour".
const juce::Colour kBackground = juce::Colours::black;

/// A flat magnitude at one level, so where its ink lands is decided entirely
/// by `levelDb` and can be predicted from PlotGeometry::yForDb alone. A real
/// captured curve would draw over most of the plot and make "ink in the top
/// quarter" mean nothing.
Trace flatTrace(std::string id, float levelDb) {
    CaptureMeta meta;
    meta.id = std::move(id);
    meta.sampleRate = kSampleRate;
    meta.fftSize = kFftSize;

    auto trace = Trace::make(std::move(meta),
                             std::vector<float>(rta::trace::pointCountFor(kFftSize), levelDb));
    REQUIRE(trace.has_value());
    return std::move(*trace);
}

std::string addFlatTrace(TraceLibrary& library, std::string id, float levelDb) {
    auto name = "capture " + id;
    return library.add(flatTrace(std::move(id), levelDb), std::move(name), "A");
}

/// The plot rectangle starts at (0,0) so the layer's blit origin -- floor(left),
/// floor(top) -- is the image origin too, and a pixel coordinate in a test is
/// the same coordinate the layer rasterised into.
PlotGeometry plotGeometry(float right = static_cast<float>(kPlotWidth)) {
    PlotGeometry geometry;
    geometry.left = 0.0f;
    geometry.top = 0.0f;
    geometry.right = right;
    geometry.bottom = static_cast<float>(kPlotHeight);
    geometry.fLowHz = 20.0;
    geometry.fHighHz = 20000.0;
    geometry.dbTop = kDbTop;
    geometry.dbBottom = kDbBottom;
    return geometry;
}

void clearToBackground(juce::Image& image) {
    juce::Graphics g(image);
    g.fillAll(kBackground);
}

juce::Image blankPlot() {
    juce::Image image(juce::Image::ARGB, kPlotWidth, kPlotHeight, true);
    clearToBackground(image);
    return image;
}

/// The Graphics object is scoped to one draw, exactly as a paint() call would
/// be -- a layer that only looked correct because a single long-lived Graphics
/// held state across frames would pass a test that reused one.
void drawOnto(StoredTraceLayer& layer, juce::Image& image, const TraceLibrary& library,
              const PlotGeometry& geometry) {
    juce::Graphics g(image);
    layer.draw(g, library, geometry);
}

int inkPixels(const juce::Image& image, juce::Rectangle<int> area) {
    int count = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y) {
        for (int x = area.getX(); x < area.getRight(); ++x) {
            if (image.getPixelAt(x, y) != kBackground) ++count;
        }
    }
    return count;
}

juce::Rectangle<int> wholePlot() { return { 0, 0, kPlotWidth, kPlotHeight }; }
juce::Rectangle<int> topQuarter() { return { 0, 0, kPlotWidth, kPlotHeight / 4 }; }
juce::Rectangle<int> bottomQuarter() {
    return { 0, kPlotHeight * 3 / 4, kPlotWidth, kPlotHeight / 4 };
}

}  // namespace

// CATCHES: a draw() that rasterises into its cached image and then never
// composites it, and a rebuild() that produces an empty image for a library
// that plainly has something to show. Both are invisible to every other test
// here -- rebuildCount() moves identically whether or not a single pixel
// reaches the destination.
TEST_CASE("a visible stored trace puts ink on the plot image", "[stored-trace-layer]") {
    TraceLibrary library;
    REQUIRE_FALSE(addFlatTrace(library, "t1", -45.0f).empty());

    auto image = blankPlot();

    // Asserted BEFORE as well as after: a broken inkPixels that always
    // returned zero would make the "> 0" below unreachable, and one that
    // always returned a positive count would make it vacuous. Only both
    // directions together prove the measurement responds to the drawing.
    REQUIRE(inkPixels(image, wholePlot()) == 0);

    StoredTraceLayer layer;
    drawOnto(layer, image, library, plotGeometry());

    CHECK(inkPixels(image, wholePlot()) > 0);
}

// CATCHES: the O(N)-per-frame regression this whole class exists to prevent --
// a draw() that calls rebuild() unconditionally, or one whose cache key
// includes something that changes every frame. The rendered plot is
// pixel-identical either way, so nothing but this counter can tell a working
// cache from a decorative one.
TEST_CASE("drawing twice with nothing changed does not rebuild", "[stored-trace-layer]") {
    TraceLibrary library;
    REQUIRE_FALSE(addFlatTrace(library, "t1", -45.0f).empty());

    auto image = blankPlot();
    const auto geometry = plotGeometry();
    StoredTraceLayer layer;

    REQUIRE(layer.rebuildCount() == 0);
    drawOnto(layer, image, library, geometry);
    REQUIRE(layer.rebuildCount() == 1);

    const auto afterFirst = layer.rebuildCount();
    drawOnto(layer, image, library, geometry);
    CHECK(layer.rebuildCount() == afterFirst);

    // A third frame, because a cache that alternates -- rebuild, reuse,
    // rebuild -- would satisfy a single repeat and still cost half the frames.
    drawOnto(layer, image, library, geometry);
    CHECK(layer.rebuildCount() == afterFirst);
}

// CATCHES: a cache key that omits TraceLibrary::revision(). Hiding a trace
// would then leave it on screen until something unrelated -- a window resize --
// happened to invalidate the image, which is the worst kind of bug: it looks
// like the checkbox is broken only sometimes. "Exactly one" also catches the
// opposite error, a key so unstable that the edit rebuilds and the next frame
// rebuilds again.
TEST_CASE("a library edit rebuilds the image exactly once", "[stored-trace-layer]") {
    TraceLibrary library;
    const auto id = addFlatTrace(library, "t1", -45.0f);
    REQUIRE_FALSE(id.empty());

    auto image = blankPlot();
    const auto geometry = plotGeometry();
    StoredTraceLayer layer;

    drawOnto(layer, image, library, geometry);
    const auto beforeEdit = layer.rebuildCount();

    // A REAL flip: TraceLibrary deliberately does not advance its revision for
    // a setter that changes nothing, so setVisible(id, true) here would prove
    // nothing about the layer.
    REQUIRE(library.setVisible(id, false));

    drawOnto(layer, image, library, geometry);
    CHECK(layer.rebuildCount() == beforeEdit + 1);
}

// CATCHES: a cache keyed on the library alone. The image is rasterised for one
// exact pixel rectangle and blitted back at the origin it was rasterised for,
// so reusing it after a resize puts every trace at the wrong frequency and at
// the wrong dB -- and nothing later corrects it, because the library has not
// changed and never will just because a window moved.
TEST_CASE("a geometry change rebuilds the image", "[stored-trace-layer]") {
    TraceLibrary library;
    REQUIRE_FALSE(addFlatTrace(library, "t1", -45.0f).empty());

    auto image = blankPlot();
    StoredTraceLayer layer;

    drawOnto(layer, image, library, plotGeometry());
    const auto beforeResize = layer.rebuildCount();

    // Same image, same library, narrower plot.
    drawOnto(layer, image, library, plotGeometry(static_cast<float>(kPlotWidth) - 40.0f));
    CHECK(layer.rebuildCount() == beforeResize + 1);
}

// CATCHES: a cache keyed on revision() WITHOUT the library's identity. Both
// libraries below sit at revision 1 after their single add(), so a
// revision-only key reads "nothing changed" and blits the first library's
// picture for the second -- a plot showing a measurement that is not loaded,
// with no error anywhere. The counter catches the mechanism; the ink-position
// assertions catch the consequence, so this test still fails if some future
// key is wrong in a way that happens to leave the counter alone.
TEST_CASE("swapping to a different library at the same revision rebuilds",
          "[stored-trace-layer]") {
    TraceLibrary first;
    TraceLibrary second;
    REQUIRE_FALSE(addFlatTrace(first, "t1", -5.0f).empty());   // near dbTop -> top of plot
    REQUIRE_FALSE(addFlatTrace(second, "t2", -85.0f).empty());  // near dbBottom -> bottom
    REQUIRE(first.revision() == second.revision());

    auto image = blankPlot();
    const auto geometry = plotGeometry();
    StoredTraceLayer layer;

    drawOnto(layer, image, first, geometry);
    REQUIRE(inkPixels(image, topQuarter()) > 0);
    REQUIRE(inkPixels(image, bottomQuarter()) == 0);

    const auto beforeSwap = layer.rebuildCount();
    clearToBackground(image);
    drawOnto(layer, image, second, geometry);

    CHECK(layer.rebuildCount() == beforeSwap + 1);
    CHECK(inkPixels(image, bottomQuarter()) > 0);
    CHECK(inkPixels(image, topQuarter()) == 0);
}

// CATCHES: a rebuild() that ignores LibraryEntry::visible. Nobody reviewing a
// screenshot can tell a trace that should be hidden from one drawn at a shade
// they did not expect, so "the visibility toggle does nothing" survives visual
// review indefinitely. The rebuildCount assertion is part of the claim: the
// layer must have CONSIDERED this library and found nothing to draw, not
// skipped it and left a stale image behind.
TEST_CASE("a hidden trace contributes no ink", "[stored-trace-layer]") {
    TraceLibrary library;
    const auto id = addFlatTrace(library, "t1", -45.0f);
    REQUIRE_FALSE(id.empty());
    REQUIRE(library.setVisible(id, false));

    auto image = blankPlot();
    StoredTraceLayer layer;
    drawOnto(layer, image, library, plotGeometry());

    CHECK(inkPixels(image, wholePlot()) == 0);
    CHECK(layer.rebuildCount() == 1);
}
