// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rtatool_view_tests. Decisions 1-5 of
// docs/dsp/2026-08-29-display-layer-l5c.md.
//
// TransferView is the largest component in the lane, so this file is
// deliberately picky about what counts as a real assertion: every test below
// names, in its own comment, the wrong implementation it is built to catch
// (project CLAUDE.md, "what wrong implementation would make this red?").
// Pixel checks distinguish the LIVE trace's warm sodium-amber ink from the
// grid's cool grey hairlines and the STORED trace's cool grey ink by channel
// balance (R-B), grounded in ui/az_ui/theme/Palette.h's actual token values
// (accentArgb 0xffff9f1c, borderArgb 0xff2b2f37, dimArgb 0xff868d98) rather
// than by re-deriving the alpha blending arithmetic TraceStroke.cpp owns --
// a golden-image compare would lock in today's antialiasing and fail on the
// next unrelated palette tweak (test_stored_trace_layer.cpp's own reasoning).
//
// No window, no desktop peer, no message loop -- the same headless software-
// renderer path tools/snapshot.cpp already proves runs offscreen.
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <az_ui/az_ui.h>

#include "measure/Snapshot.h"
#include "measure/SnapshotSource.h"
#include "measure/SyntheticSnapshot.h"
#include "trace/Trace.h"
#include "trace/TraceLibrary.h"
#include "view/BodeLayout.h"
#include "view/MeasureColours.h"
#include "view/PlotGeometry.h"
#include "view/TransferView.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using rta::measure::Snapshot;
using rta::measure::SnapshotPtr;
using rta::measure::StaticSnapshotSource;
using rta::trace::CaptureMeta;
using rta::trace::Trace;
using rta::trace::TraceLibrary;
using rta::view::BodePanes;
using rta::view::FrequencyAxis;
using rta::view::PaneRect;
using rta::view::PlotGeometry;
using rta::view::TransferView;

const juce::Colour kBackground = az::ui::background;

/// A snapshot carrying a real, deterministic transfer block -- the fixture
/// every test below except the empty-state one renders.
SnapshotPtr snapshotWithTransfer(std::size_t fftSize, double sampleRate, int delaySamples) {
    auto snap = std::make_shared<Snapshot>();
    snap->sequence = 1;
    snap->sampleRate = sampleRate;
    snap->fftSize = fftSize;
    snap->transfer = rta::measure::makeSyntheticTransfer(fftSize, sampleRate, delaySamples);
    return snap;
}

/// An RTA-only snapshot -- bands present, no reference channel ever fed, so
/// `transfer` is genuinely absent rather than empty. Matches how `Analyser`
/// itself would leave the field before the first `pushPair`.
SnapshotPtr rtaOnlySnapshot() {
    auto snap = std::make_shared<Snapshot>();
    snap->sequence = 1;
    snap->sampleRate = 48000.0;
    snap->fftSize = 4096;
    return snap;
}

/// True for a pixel that could only be the LIVE trace's sodium-amber ink
/// (accentArgb 0xffff9f1c, R=255 B=28), never the grid's cool grey
/// (borderArgb 0xff2b2f37, R=43 B=55) or the stored trace's cool grey
/// (dimArgb 0xff868d98, R=134 B=152) -- both of the latter have R <= B.
/// True even at the alpha floor (0.25): blended 25% over the near-black
/// background (0x0a0b0d) still lands at roughly R=71 B=17, R-B=54.
bool isLiveTraceish(juce::Colour c) {
    return static_cast<int>(c.getRed()) - static_cast<int>(c.getBlue()) > 30;
}

/// Within a small per-channel tolerance of `target`. A 1px-tall extent (a
/// flat trace: min == max, so `fillColumn`'s height is exactly one row) is
/// still drawn through the software renderer's float-coordinate `fillRect`,
/// which shaves a few levels off an otherwise-opaque fill even at an
/// integer-aligned position -- an exact `==` would fail on every such pixel
/// though the fill is visibly, unambiguously the target colour. A taller
/// extent (a real dB range, several pixels high) has interior rows an exact
/// match WOULD catch; a degenerate one has none, so the tolerance is what
/// makes the assertion test the colour rather than the renderer's rounding.
bool closeTo(juce::Colour c, juce::Colour target, int tolerance) {
    return std::abs(static_cast<int>(c.getRed()) - static_cast<int>(target.getRed())) <= tolerance
        && std::abs(static_cast<int>(c.getGreen()) - static_cast<int>(target.getGreen())) <= tolerance
        && std::abs(static_cast<int>(c.getBlue()) - static_cast<int>(target.getBlue())) <= tolerance;
}

bool anyPixelDiffers(const juce::Image& image, juce::Rectangle<int> area, juce::Colour from) {
    for (int y = area.getY(); y < area.getBottom(); ++y) {
        for (int x = area.getX(); x < area.getRight(); ++x) {
            if (image.getPixelAt(x, y) != from) return true;
        }
    }
    return false;
}

bool anyPixelMatches(const juce::Image& image, juce::Rectangle<int> area,
                     const std::function<bool(juce::Colour)>& predicate) {
    for (int y = std::max(0, area.getY()); y < std::min(image.getHeight(), area.getBottom()); ++y) {
        for (int x = std::max(0, area.getX()); x < std::min(image.getWidth(), area.getRight()); ++x) {
            if (predicate(image.getPixelAt(x, y))) return true;
        }
    }
    return false;
}

juce::Image renderView(TransferView& view, int width, int height) {
    juce::Image image(juce::Image::ARGB, width, height, true);
    juce::Graphics g(image);
    view.renderTo(g, juce::Rectangle<int>(0, 0, width, height));
    return image;
}

}  // namespace

// CATCHES: a pane split that stopped tracking 5:3 (a regression in
// bodePanes itself, or TransferView.cpp calling it with the wrong content
// rectangle -- e.g. forgetting to reserve the ribbon or the gap before
// splitting), and a ribbon that scales with the window instead of staying
// furniture. Reads geometry back from `panes()`, not pixels, per the
// record's own instruction: a pixel test here would pass for a view that
// drew the right shape in the wrong place. `resized()` is called
// EXPLICITLY (mirroring tools/snapshot.cpp) because `setSize()` alone does
// not call it on a component with no desktop peer -- the exact trap this
// task's own brief names.
TEST_CASE("the phase pane keeps 3/8 of the shared area at every window size", "[transfer-view]") {
    const StaticSnapshotSource source;
    TransferView view(source);

    for (const auto& [w, h] : { std::pair{ 1100, 760 }, std::pair{ 1100, 1000 }, std::pair{ 900, 420 } }) {
        view.setSize(w, h);
        view.resized();
        const BodePanes& panes = view.panes();

        CHECK(panes.ribbon.height == rta::view::kRibbonHeight);
        REQUIRE(panes.magnitude.height > 0);
        REQUIRE(panes.phase.height > 0);
        // Same derivation and bound as test_bode_layout.cpp's "magnitude and
        // phase split the remaining height 5:3" -- see that file for why 7
        // is the tight, derived bound rather than a chosen tolerance.
        const int cross = panes.magnitude.height * 3 - panes.phase.height * 5;
        CHECK(std::abs(cross) <= 7);
    }
}

// CATCHES: a `renderTo` that reads `getLocalBounds()` or any other
// component-owned state instead of its `area` parameter -- exactly the
// "setSize() does not call resized() on a peerless component" trap project
// CLAUDE.md records, which would make `tools/snapshot.cpp`'s transfer.png
// come out blank while every OTHER test in this file (which happens to also
// call resized(), or renders through paint()) still passes.
TEST_CASE("renderTo draws without resized() ever having run", "[transfer-view]") {
    const auto snap = snapshotWithTransfer(2048, 48000.0, 15);
    const StaticSnapshotSource source(snap);
    TransferView view(source);  // no setSize(), no resized(), no desktop peer

    const auto image = renderView(view, 1100, 760);

    // Not "any pixel differs" -- that would also pass for a `renderTo` that
    // fell back to `getLocalBounds()` (0x0 on a peerless, never-sized
    // component): a degenerate layout still draws a border rect pinned near
    // the origin, leaving a FEW non-background pixels there and nothing
    // resembling a real composite. This checks a region near the FAR corner
    // of the requested 1100x760 area instead -- unreachable by any layout
    // built from a 0x0 rectangle, reachable only by one that actually used
    // the `area` parameter this method was given.
    CHECK(anyPixelDiffers(image, juce::Rectangle<int>(900, 700, 150, 50), kBackground));
}

// CATCHES: a missing-transfer branch that draws a flat trace anyway (e.g. a
// magnitude of 0 dB drawn for every column because a stale/default
// TransferBlock leaked through), which the record calls out explicitly: "A
// Bode plot of nothing looks like a measurement." The magnitude pane's own
// rectangle is read from panes() (resized() called explicitly, same reason
// as the first test), so this does not depend on this file's own layout
// arithmetic agreeing with the implementation's.
TEST_CASE("a snapshot with no transfer draws an empty state, not an empty Bode", "[transfer-view]") {
    const StaticSnapshotSource source(rtaOnlySnapshot());
    TransferView view(source);
    view.setSize(1100, 760);
    view.resized();
    const BodePanes panes = view.panes();

    const auto image = renderView(view, 1100, 760);

    // Something was drawn (the grid, and the "NO REFERENCE CHANNEL" line).
    CHECK(anyPixelDiffers(image, image.getBounds(), kBackground));

    // Nothing LIVE-TRACE-coloured landed inside the magnitude pane -- the
    // grid and the empty-state text are both cool/dim tones (checked by
    // isLiveTraceish's own contract above), so this specifically catches a
    // drawn trace, not "the pane isn't blank" (which the grid alone would
    // already satisfy, making a plain non-background check vacuous here).
    juce::Rectangle<int> magnitudeArea(panes.magnitude.x, panes.magnitude.y, panes.magnitude.width,
                                       panes.magnitude.height);
    CHECK_FALSE(anyPixelMatches(image, magnitudeArea, isLiveTraceish));
}

// CATCHES: alpha applied backwards (a low-trust column drawing BRIGHTER),
// alpha ignored (both columns drawing identically), and the per-column
// MINIMUM being read as a maximum or an average -- record section 8's own
// "assert the drawn alpha differs between high- and low-gamma^2 columns",
// at the composite level rather than TraceStroke's unit level (task 7
// already covers that).
TEST_CASE("low coherence draws dimmer than high coherence", "[transfer-view]") {
    // makeSyntheticTransfer's own closed form: flat, high coherence through
    // the midband (~0.97 at 1 kHz) and a deep LF dip that has not yet
    // recovered at 30 Hz (~0.38) -- see SyntheticSnapshot.cpp's
    // `coherenceAt`. Picking frequencies from the fixture's ACTUAL curve
    // rather than an assumed shape is what keeps this test honest about
    // what it is measuring.
    constexpr std::size_t kFftSize = 8192;
    constexpr double kSampleRate = 48000.0;
    const auto snap = snapshotWithTransfer(kFftSize, kSampleRate, 5);
    const StaticSnapshotSource source(snap);
    TransferView view(source);
    view.setSize(1100, 760);
    view.resized();
    const auto image = renderView(view, 1100, 760);

    const FrequencyAxis axis = rta::view::frequencyAxis(PaneRect{ 0, 0, 1100, 760 });
    const PlotGeometry magnitudeGeometry =
        rta::view::paneGeometry(axis, view.panes().magnitude, 18.0, -18.0);

    const int xHighTrust = static_cast<int>(magnitudeGeometry.xForHz(1000.0));
    const int xLowTrust = static_cast<int>(magnitudeGeometry.xForHz(30.0));

    // Scan each column's full magnitude-pane height for the brightest
    // live-trace pixel actually drawn there, rather than guessing a row --
    // the fixture's magnitude curve puts the two columns at different dB,
    // hence different rows, and only the RED channel (alpha strength) is
    // being compared, not position.
    const auto brightestRed = [&](int x) {
        juce::uint8 best = 0;
        for (int y = static_cast<int>(magnitudeGeometry.top); y < static_cast<int>(magnitudeGeometry.bottom); ++y) {
            const auto c = image.getPixelAt(x, y);
            if (isLiveTraceish(c)) best = std::max(best, c.getRed());
        }
        return best;
    };

    const auto highTrustRed = brightestRed(xHighTrust);
    const auto lowTrustRed = brightestRed(xLowTrust);

    REQUIRE(highTrustRed > 0);
    REQUIRE(lowTrustRed > 0);
    CHECK(lowTrustRed < highTrustRed);
}

// CATCHES: StoredTraceLayer's phase dispatch never being exercised by any
// existing test (task 7 tested TraceStroke directly, not
// StoredTraceLayer::draw with Field::Phase) -- specifically a `field_ ==
// Field::Phase` branch that accidentally calls `bridgeGaps` (decision 5
// forbids it: interpolating across a wrap invents a sweep nothing measured),
// forgets to feed coherence through, or never composites at all. A flat
// phase trace with no coherence set makes the expected pixel PREDICTABLE
// (a 1px-tall row at `paneGeometry(..., 180, -180).yForDb(90)`, opaque
// `storedTrace`) with no wrap-straddle uncertainty to account for; the match
// is a tolerance, not `==` (see `closeTo`'s own comment) because a 1px-tall
// fill has no antialiasing-free interior pixel the way a taller extent does.
TEST_CASE("a stored trace's phase renders through the cached layer", "[transfer-view]") {
    constexpr std::size_t kFftSize = 2048;
    constexpr double kSampleRate = 48000.0;

    TraceLibrary library;
    CaptureMeta meta;
    meta.id = "stored-1";
    meta.sampleRate = kSampleRate;
    meta.fftSize = static_cast<int>(kFftSize);
    const auto bins = rta::trace::pointCountFor(static_cast<int>(kFftSize));

    auto trace = Trace::make(meta, std::vector<float>(bins, -6.0f));
    REQUIRE(trace.has_value());
    // Flat 90 degrees everywhere: no wrap along the bin axis is possible (a
    // constant has zero delta between neighbours), so the expected drawn
    // row is exactly `paneGeometry(..., 180, -180).yForDb(90)`.
    REQUIRE(trace->setPhase(std::vector<float>(bins, 90.0f)));
    // Coherence deliberately NOT set: TraceStroke.h's empty-span contract
    // ("no coherence measured") draws fully opaque.
    REQUIRE_FALSE(library.add(std::move(*trace), "stored", "A").empty());

    const auto snap = snapshotWithTransfer(kFftSize, kSampleRate, 10);
    const StaticSnapshotSource source(snap);
    TransferView view(source);
    view.setLibrary(&library);
    view.setSize(1100, 760);
    view.resized();

    const auto image = renderView(view, 1100, 760);

    const FrequencyAxis axis = rta::view::frequencyAxis(PaneRect{ 0, 0, 1100, 760 });
    const PlotGeometry phaseGeometry = rta::view::paneGeometry(axis, view.panes().phase, 180.0, -180.0);
    const int expectedY = static_cast<int>(std::lround(phaseGeometry.yForDb(90.0)));

    // Scanned across most of the pane's WIDTH, not one column: bins are
    // sparse at the log axis's low-frequency end (TraceDecimator.h's own
    // "dotted scatter" comment), so a single x position could legitimately
    // land in a column no bin mapped to. A flat trace at 90 degrees has to
    // paint SOMEWHERE across a 1000-pixel-wide pane if the dispatch works at
    // all -- this is the width-independent version of that claim.
    bool found = false;
    for (int y = expectedY - 2; y <= expectedY + 2 && !found; ++y) {
        for (int x = static_cast<int>(phaseGeometry.left) + 10; x < static_cast<int>(phaseGeometry.right) - 10; ++x) {
            if (closeTo(image.getPixelAt(x, y), rta::view::storedTrace, 6)) {
                found = true;
                break;
            }
        }
    }
    CHECK(found);
}

// CATCHES: an unwrap toggle that changes the DRAWING path but leaves the
// axis fixed at +-180 (every value beyond that clamps to the floor, so a
// long, multi-turn delay would collapse to a flat line hugging the pane's
// bottom edge instead of a curve spanning the whole extended range) --
// exactly the failure "the axis extends in whole multiples of 360" (decision
// 4) exists to name. Verified by predicting where ONE bin's phase must land
// under the CORRECT extended axis (0 to -360, both exact multiples of 360
// for this delay -- worked out by hand in the comment below) versus where
// that same bin would land under the fixed +-180 axis, and confirming the
// live trace's ink follows the axis that was actually supposed to be in
// force. The second half proves unwrap is non-destructive: toggling back
// reproduces the ORIGINAL wrapped image exactly, byte for byte -- decision 4
// promises this is a display-only, non-destructive operation on the stored
// wrapped values.
TEST_CASE("unwrapping changes the axis, not the stored data", "[transfer-view]") {
    // fftSize=1024, sampleRate=48000, delaySamples=2: binHz=46.875,
    // per-bin step = 360*2/1024 = 0.7 degrees (comfortably under 180, so
    // unwrapPhase reconstructs the exact analytic line -- the same
    // precondition test_phase_unwrap.cpp's own "a pure delay unwraps to a
    // straight line" case relies on). phi(f) = -360*f*D/fs is monotonic, so
    // its extremes over the bin range are exactly phi(0)=0 and
    // phi(24000)=-360. Both are already exact multiples of 360, so decision
    // 4's enclosing-multiples rule gives dbTop=0, dbBottom=-360 -- no
    // rounding to reason about.
    constexpr std::size_t kFftSize = 1024;
    constexpr double kSampleRate = 48000.0;
    constexpr int kDelaySamples = 2;
    constexpr double kUnwrappedDbTop = 0.0;
    constexpr double kUnwrappedDbBottom = -360.0;

    const auto snap = snapshotWithTransfer(kFftSize, kSampleRate, kDelaySamples);
    const StaticSnapshotSource source(snap);
    TransferView view(source);
    view.setSize(1100, 760);
    view.resized();

    const auto wrappedBefore = renderView(view, 1100, 760);

    view.setPhaseUnwrapped(true);
    const auto unwrappedImage = renderView(view, 1100, 760);

    // bin k=300: f = 300*46.875 = 14062.5 Hz.
    // phi(f) = -360 * 14062.5 * 2 / 48000 = -210.9375 degrees (unwrapped,
    // continuous -- already past one wrap boundary).
    // Wrapped equivalent (what a still-fixed +-180 axis would show instead):
    // wrapTo180(-210.9375) = -210.9375 + 360 = 149.0625 degrees.
    constexpr double kUnwrappedValue = -210.9375;
    constexpr double kWrappedEquivalent = 149.0625;

    const FrequencyAxis axis = rta::view::frequencyAxis(PaneRect{ 0, 0, 1100, 760 });
    const PlotGeometry unwrappedGeometry =
        rta::view::paneGeometry(axis, view.panes().phase, kUnwrappedDbTop, kUnwrappedDbBottom);
    const PlotGeometry wrappedGeometry = rta::view::paneGeometry(axis, view.panes().phase, 180.0, -180.0);

    const int x = static_cast<int>(unwrappedGeometry.xForHz(14062.5));
    const int yUnderExtendedAxis = static_cast<int>(std::lround(unwrappedGeometry.yForDb(kUnwrappedValue)));
    const int yUnderFixedAxis = static_cast<int>(std::lround(wrappedGeometry.yForDb(kWrappedEquivalent)));

    // The two predicted rows are roughly half the phase pane's height apart
    // (t = 0.586 vs t = 0.086) -- comfortably separated regardless of the
    // exact pane height at 1100x760, so a generous search box still cannot
    // confuse one prediction for the other.
    REQUIRE(std::abs(yUnderExtendedAxis - yUnderFixedAxis) > 40);

    juce::Rectangle<int> extendedBox(x - 4, yUnderExtendedAxis - 20, 9, 41);
    juce::Rectangle<int> fixedBox(x - 4, yUnderFixedAxis - 20, 9, 41);

    CHECK(anyPixelMatches(unwrappedImage, extendedBox, isLiveTraceish));
    CHECK_FALSE(anyPixelMatches(unwrappedImage, fixedBox, isLiveTraceish));

    // Non-destructive: toggling back reproduces the ORIGINAL wrapped image
    // exactly. Decision 4 promises the stored wrapped values are never
    // touched -- only the display path changes -- and this is the pixel-
    // level version of that promise.
    view.setPhaseUnwrapped(false);
    const auto wrappedAfter = renderView(view, 1100, 760);

    REQUIRE(wrappedBefore.getWidth() == wrappedAfter.getWidth());
    REQUIRE(wrappedBefore.getHeight() == wrappedAfter.getHeight());
    for (int y = 0; y < wrappedBefore.getHeight(); ++y) {
        for (int px = 0; px < wrappedBefore.getWidth(); ++px) {
            if (wrappedBefore.getPixelAt(px, y) != wrappedAfter.getPixelAt(px, y)) {
                FAIL("pixel (" << px << "," << y << ") changed after unwrap round-trip");
            }
        }
    }
}
