// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rtatool_view_tests. Split out of test_transfer_view.cpp
// (which was at the project's 400-line hard cap) to make room for that file's
// margin-overpaint case; this test's own topic -- the unwrap toggle's axis
// swap -- stands on its own and needed no code it did not already bring with
// it. Same fixtures and pixel predicates, from test_transfer_view_helpers.h.
#include <catch2/catch_test_macros.hpp>

#include "test_transfer_view_helpers.h"

#include "view/BodeLayout.h"
#include "view/PlotGeometry.h"

#include <cmath>

using rta::measure::StaticSnapshotSource;
using rta::view::FrequencyAxis;
using rta::view::PaneRect;
using rta::view::PlotGeometry;

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
