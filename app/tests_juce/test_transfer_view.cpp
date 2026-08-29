// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rtatool_view_tests. Decisions 1-5 and 5a of
// docs/dsp/2026-08-29-display-layer-l5c.md.
//
// TransferView is the largest component in the lane, so this file is
// deliberately picky about what counts as a real assertion: every test below
// names, in its own comment, the wrong implementation it is built to catch
// (project CLAUDE.md, "what wrong implementation would make this red?").
// Pixel checks distinguish the LIVE trace's warm sodium-amber ink from the
// grid's cool grey and the STORED trace's cool grey by channel balance,
// grounded in ui/az_ui/theme/Palette.h's actual token values (accentArgb
// 0xffff9f1c, borderArgb 0xff2b2f37, dimArgb 0xff868d98) rather than by
// re-deriving TraceStroke.cpp's own alpha blending -- a golden-image compare
// would lock in today's antialiasing and fail the next unrelated palette
// tweak (test_stored_trace_layer.cpp's own reasoning). Fixtures and pixel
// predicates live in test_transfer_view_helpers.h, split out to keep this
// file under the project's 400-line hard cap.
//
// No window, no desktop peer, no message loop -- the same headless software-
// renderer path tools/snapshot.cpp already proves runs offscreen.
#include <catch2/catch_test_macros.hpp>

#include "test_transfer_view_helpers.h"

#include "trace/Trace.h"
#include "trace/TraceLibrary.h"
#include "view/BodeLayout.h"
#include "view/MeasureColours.h"
#include "view/PlotGeometry.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

using rta::measure::StaticSnapshotSource;
using rta::trace::CaptureMeta;
using rta::trace::Trace;
using rta::trace::TraceLibrary;
using rta::view::BodePanes;
using rta::view::FrequencyAxis;
using rta::view::PaneRect;
using rta::view::PlotGeometry;

// CATCHES: a pane split that stopped tracking 5:3, and a ribbon that scales
// with the window instead of staying furniture. Reads geometry back from
// `panes()`, not pixels -- a pixel test would pass for a view that drew the
// right shape in the wrong place. `resized()` is called EXPLICITLY because
// `setSize()` alone does not call it on a component with no desktop peer.
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
// stale/default TransferBlock leaked through) -- "A Bode plot of nothing
// looks like a measurement." The magnitude rectangle is read from panes(),
// so this does not depend on this file's own layout arithmetic agreeing.
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
    // the midband (~0.97 at 1 kHz) and a deep LF dip not yet recovered at
    // 30 Hz (~0.38) -- see SyntheticSnapshot.cpp's `coherenceAt`. Frequencies
    // taken from the fixture's ACTUAL curve, not an assumed shape.
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
    const int top = static_cast<int>(magnitudeGeometry.top);
    const int bottom = static_cast<int>(magnitudeGeometry.bottom);

    const int xHighTrust = static_cast<int>(magnitudeGeometry.xForHz(1000.0));
    const int xLowTrust = static_cast<int>(magnitudeGeometry.xForHz(30.0));

    // The magnitude curve puts the two columns at different dB, hence
    // different rows -- brightestLiveTraceRed scans the whole column so only
    // the RED channel (alpha strength) is compared, not position.
    const auto highTrustRed = brightestLiveTraceRed(image, xHighTrust, top, bottom);
    const auto lowTrustRed = brightestLiveTraceRed(image, xLowTrust, top, bottom);

    REQUIRE(highTrustRed > 0);
    REQUIRE(lowTrustRed > 0);
    CHECK(lowTrustRed < highTrustRed);
}

// CATCHES: a composite that re-rasterises a stored layer every frame
// regardless of what changed -- invisible to every pixel test above (a
// rebuilt image and a reused one are bit-identical), surfacing only as
// dropped frames at a live show.
TEST_CASE("advancing the live snapshot rebuilds neither cached layer", "[transfer-view]") {
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
    REQUIRE(trace->setPhase(std::vector<float>(bins, 45.0f)));
    const auto id = library.add(std::move(*trace), "stored", "A");
    REQUIRE_FALSE(id.empty());

    StaticSnapshotSource source(snapshotWithTransfer(kFftSize, kSampleRate, 10));
    TransferView view(source);
    view.setLibrary(&library);
    view.setSize(1100, 760);
    view.resized();

    renderView(view, 1100, 760);
    const auto magAfterFirst = view.magnitudeLayer().rebuildCount();
    const auto phaseAfterFirst = view.phaseLayer().rebuildCount();
    REQUIRE(magAfterFirst >= 1);
    REQUIRE(phaseAfterFirst >= 1);

    // Advancing the LIVE snapshot several times, library untouched, must not
    // move either count -- the pixels a stored layer produces are identical
    // whether the image was reused or re-rasterised, so only this counter
    // can tell the two apart.
    for (int i = 0; i < 3; ++i) {
        source.set(snapshotWithTransfer(kFftSize, kSampleRate, 10 + i));
        renderView(view, 1100, 760);
    }
    CHECK(view.magnitudeLayer().rebuildCount() == magAfterFirst);
    CHECK(view.phaseLayer().rebuildCount() == phaseAfterFirst);

    // A REAL library edit -- TraceLibrary setters no-op when the value is
    // unchanged, so this renames to something actually different.
    REQUIRE(library.rename(id, "renamed"));
    renderView(view, 1100, 760);
    CHECK(view.magnitudeLayer().rebuildCount() == magAfterFirst + 1);
    CHECK(view.phaseLayer().rebuildCount() == phaseAfterFirst + 1);
}

// CATCHES: StoredTraceLayer's phase dispatch, never exercised before this
// lane (task 7 tested TraceStroke directly): a `field_ == Field::Phase`
// branch that wrongly calls `bridgeGaps` (decision 5 forbids it -- bridging
// a gap invents a value nothing measured) or that forgets to feed coherence
// through. A FLAT fixture catches neither: interpolating between two EQUAL
// values reproduces those values, and with coherence unset the opaque and
// the faded implementations draw identically. So: two distinct phase levels
// either side of a genuine bin gap (the log axis's own sparse low-frequency
// density, TraceDecimator.h's "dotted scatter"), and a LOW coherence.
TEST_CASE("a stored trace's phase renders through the cached layer, gap and trust intact",
          "[transfer-view]") {
    constexpr std::size_t kFftSize = 2048;
    constexpr double kSampleRate = 48000.0;
    constexpr float kLevelA = 0.0f;
    constexpr float kLevelB = 90.0f;
    constexpr float kLowGammaSquared = 0.3f;

    TraceLibrary library;
    CaptureMeta meta;
    meta.id = "stored-1";
    meta.sampleRate = kSampleRate;
    meta.fftSize = static_cast<int>(kFftSize);
    const auto bins = rta::trace::pointCountFor(static_cast<int>(kFftSize));

    auto trace = Trace::make(meta, std::vector<float>(bins, -6.0f));
    REQUIRE(trace.has_value());

    // Bins 0-5 (DC through ~117 Hz) at kLevelA, bins 6+ at kLevelB. At this
    // fftSize/sampleRate the log axis maps bin 5 and bin 6 roughly 25 pixel
    // columns apart with nothing between them -- verified below with the
    // SAME xForHz the implementation itself uses, not a hand count.
    std::vector<float> phase(bins, kLevelB);
    for (std::size_t i = 0; i <= 5 && i < bins; ++i) phase[i] = kLevelA;
    REQUIRE(trace->setPhase(phase));
    REQUIRE(trace->setCoherence(std::vector<float>(bins, kLowGammaSquared)));
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
    const double binHz = kSampleRate / static_cast<double>(kFftSize);

    const int xLastA = static_cast<int>(phaseGeometry.xForHz(5.0 * binHz));   // bin 5
    const int xFirstB = static_cast<int>(phaseGeometry.xForHz(6.0 * binHz));  // bin 6
    REQUIRE(xFirstB > xLastA + 4);  // a REAL gap, not adjacent columns

    const int yLevelA = static_cast<int>(std::lround(phaseGeometry.yForDb(kLevelA)));
    const int yLevelB = static_cast<int>(std::lround(phaseGeometry.yForDb(kLevelB)));

    // Both levels render AND at LOW-coherence dimness: green capped well
    // below opaque `storedTrace`'s 141 -- `anyPixelMatches` alone would also
    // pass for a branch that forgets to feed coherence through and draws
    // fully opaque, since opaque ink still matches `isStoredTraceish`.
    constexpr juce::uint8 kOpaqueGreen = 141;
    const auto greenA =
        brightestMatchingGreen(image, juce::Rectangle<int>(xLastA - 2, yLevelA - 2, 5, 5), isStoredTraceish);
    const auto greenB =
        brightestMatchingGreen(image, juce::Rectangle<int>(xFirstB - 2, yLevelB - 2, 5, 5), isStoredTraceish);
    REQUIRE(greenA > 0);
    REQUIRE(greenB > 0);
    CHECK(greenA < kOpaqueGreen - 25);
    CHECK(greenB < kOpaqueGreen - 25);

    // Nothing at all in the gap, at ANY row: a bridged implementation would
    // paint an interpolated degree there; a correct one leaves it untouched.
    juce::Rectangle<int> gapArea(xLastA + 2, static_cast<int>(phaseGeometry.top), xFirstB - xLastA - 4,
                                 static_cast<int>(phaseGeometry.bottom - phaseGeometry.top));
    CHECK_FALSE(anyPixelMatches(image, gapArea, isStoredTraceish));
}

// CATCHES: a ribbon that ignores coherence (every column drawn at the same
// alpha) or reads gamma^2 backwards -- the ribbon had no pixel test at all
// before this lane, and decision 3 gives it exactly one job to check.
TEST_CASE("a low-coherence ribbon column is dimmer than a midband one", "[transfer-view]") {
    // Same fixture/frequencies as "low coherence draws dimmer" above.
    constexpr std::size_t kFftSize = 8192;
    constexpr double kSampleRate = 48000.0;
    const auto snap = snapshotWithTransfer(kFftSize, kSampleRate, 5);
    const StaticSnapshotSource source(snap);
    TransferView view(source);
    view.setSize(1100, 760);
    view.resized();
    const auto image = renderView(view, 1100, 760);

    const FrequencyAxis axis = rta::view::frequencyAxis(PaneRect{ 0, 0, 1100, 760 });
    const PlotGeometry ribbonGeometry = rta::view::paneGeometry(axis, view.panes().ribbon, 1.0, 0.0);

    const int xHighTrust = static_cast<int>(ribbonGeometry.xForHz(1000.0));
    const int xLowTrust = static_cast<int>(ribbonGeometry.xForHz(30.0));
    const int yMid = view.panes().ribbon.y + view.panes().ribbon.height / 2;

    const auto highTrust = image.getPixelAt(xHighTrust, yMid);
    const auto lowTrust = image.getPixelAt(xLowTrust, yMid);

    // Still visible, not deleted -- the alpha floor's own promise.
    CHECK(lowTrust != kBackground);
    CHECK(lowTrust.getRed() < highTrust.getRed());
}

// CATCHES: an unwrap toggle that changes the DRAWING path but leaves the axis
// fixed at +-180 (every value beyond that clamps to the floor, collapsing a
// long delay to a flat line at the pane's bottom edge) -- the failure
// "the axis extends in whole multiples of 360" (decision 4) exists to name.
// Verified by predicting where ONE bin's phase lands under the CORRECT
// extended axis versus the fixed +-180 one (worked out by hand below), and
// confirming the ink follows the axis actually in force. The second half
// proves unwrap is non-destructive: toggling back reproduces the ORIGINAL
// wrapped image byte for byte, per decision 4's promise.
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
