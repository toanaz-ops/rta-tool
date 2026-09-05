// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rtatool_view_tests. Task 7 of
// docs/plans/2026-09-05-L3-mtw-impl-plan.md: the per-plot source toggle and
// the seam marks. Fixtures here are built directly (not through
// makeSyntheticTransfer) so the fixed and MTW blocks can be made DELIBERATELY
// different -- a flat fixed curve against an MTW ramp -- per the plan's own
// R11 warning: "the view draws the fixed block and calls it MTW" must not be
// satisfiable by two identical-looking curves.
#include <catch2/catch_test_macros.hpp>

#include "test_transfer_view_helpers.h"

#include "rta/dsp/MtwLayout.h"
#include "view/BodeLayout.h"
#include "view/PlotGeometry.h"

#include <cmath>
#include <vector>

using rta::measure::MtwBandDescriptor;
using rta::measure::MtwBlock;
using rta::measure::Snapshot;
using rta::measure::SnapshotPtr;
using rta::measure::StaticSnapshotSource;
using rta::measure::TransferBlock;
using rta::view::BodePanes;
using rta::view::FrequencyAxis;
using rta::view::PaneRect;
using rta::view::PlotGeometry;
using rta::view::TransferPane;
using rta::view::TransferSource;
using rta::view::TransferView;

namespace {

/// Real layout (default MtwConfig: topFftSize 1024, octaveCount 6, 48 kHz) --
/// 1281 points, seams at 187.5/375/750/1500/3000/6000 Hz, matching the record
/// this lane implements rather than a fixture built to fit the test.
MtwBlock makeMtwFixture() {
    const rta::dsp::MtwConfig cfg;
    const auto bands = rta::dsp::mtwBands(cfg);
    const auto freq = rta::dsp::mtwFrequencies(cfg);

    MtwBlock block;
    block.frequencyHz = freq;
    const std::size_t n = freq.size();
    block.magnitudeDb.resize(n);
    // A visible RAMP, -12 dB to +12 dB across the curve -- deliberately NOT
    // the fixed fixture's flat 0 dB, so a view that drew the fixed block and
    // called it MTW would be caught by the magnitude test below.
    for (std::size_t i = 0; i < n; ++i) {
        const double t = n > 1 ? static_cast<double>(i) / static_cast<double>(n - 1) : 0.0;
        block.magnitudeDb[i] = static_cast<float>(-12.0 + 24.0 * t);
    }
    // A flat, but non-zero, phase -- distinguishable from the fixed
    // fixture's flat 0 deg so the "phase stays Mtw" half of case 4 has
    // something real to check.
    block.phaseDeg.assign(n, -90.0f);
    block.coherence.assign(n, 1.0f);

    block.bands.reserve(bands.size());
    for (const auto& band : bands) {
        MtwBandDescriptor d;
        d.firstIndex = band.firstIndex;
        d.pointCount = band.lastBin - band.firstBin + 1;
        d.fftSize = band.fftSize;
        d.windowSeconds = static_cast<float>(band.windowSeconds);
        d.integrationSeconds = static_cast<float>(band.integrationSeconds);
        d.effectiveAverages = 8.5866271;
        d.seamHz = static_cast<float>(band.lowerEdgeHz);
        d.coherenceAvailable = true;
        block.bands.push_back(d);
    }
    return block;
}

TransferBlock makeFixedFlatFixture(std::size_t bins) {
    TransferBlock block;
    block.magnitudeDb.assign(bins, 0.0f);
    block.phaseDeg.assign(bins, 0.0f);
    return block;
}

/// A snapshot carrying BOTH blocks, visibly different from each other.
SnapshotPtr snapshotWithBoth(std::size_t fftSize, double sampleRate) {
    auto snap = std::make_shared<Snapshot>();
    snap->sequence = 1;
    snap->sampleRate = sampleRate;
    snap->fftSize = fftSize;
    snap->transfer = makeFixedFlatFixture(fftSize / 2 + 1);
    snap->mtw = makeMtwFixture();
    return snap;
}

/// The y of the first live-trace-coloured pixel found scanning down column
/// `x`, or -1 if none.
int firstLiveTraceY(const juce::Image& image, int x, int yTop, int yBottom) {
    for (int y = yTop; y < yBottom; ++y) {
        if (isLiveTraceish(image.getPixelAt(x, y))) return y;
    }
    return -1;
}

}  // namespace

// CATCHES: a magnitude pane that draws the fixed (flat) block regardless of
// the toggle (plan's R11) -- a flat curve puts the low- and high-frequency
// columns at the SAME row; the MTW ramp fixture puts them many rows apart.
TEST_CASE("the transfer view draws the MTW block by default", "[transferview][mtw]") {
    const auto snap = snapshotWithBoth(4096, 48000.0);
    const StaticSnapshotSource source(snap);
    TransferView view(source);
    view.setSize(1100, 760);
    view.resized();

    CHECK(view.source(TransferPane::Magnitude) == TransferSource::Mtw);
    CHECK(view.source(TransferPane::Phase) == TransferSource::Mtw);
    CHECK(view.source(TransferPane::Coherence) == TransferSource::Mtw);

    const auto image = renderView(view, 1100, 760);
    const FrequencyAxis axis = rta::view::frequencyAxis(PaneRect{ 0, 0, 1100, 760 });
    const PlotGeometry magnitudeGeometry =
        rta::view::paneGeometry(axis, view.panes().magnitude, 18.0, -18.0);
    const int top = static_cast<int>(magnitudeGeometry.top);
    const int bottom = static_cast<int>(magnitudeGeometry.bottom);

    const int xLow = static_cast<int>(magnitudeGeometry.xForHz(30.0));
    const int xHigh = static_cast<int>(magnitudeGeometry.xForHz(15000.0));

    const int yLow = firstLiveTraceY(image, xLow, top, bottom);
    const int yHigh = firstLiveTraceY(image, xHigh, top, bottom);
    REQUIRE(yLow >= 0);
    REQUIRE(yHigh >= 0);
    CHECK(std::abs(yLow - yHigh) > 20);
}

// CATCHES: a per-pane toggle that is ignored, or one that flips every pane
// together instead of the one named.
TEST_CASE("the per-plot source toggle switches magnitude back to the fixed FFT",
          "[transferview][mtw]") {
    const auto snap = snapshotWithBoth(4096, 48000.0);
    const StaticSnapshotSource source(snap);
    TransferView view(source);
    view.setSize(1100, 760);
    view.resized();

    view.setSource(TransferPane::Magnitude, TransferSource::Fixed);
    REQUIRE(view.source(TransferPane::Magnitude) == TransferSource::Fixed);
    REQUIRE(view.source(TransferPane::Phase) == TransferSource::Mtw);

    const auto image = renderView(view, 1100, 760);
    const FrequencyAxis axis = rta::view::frequencyAxis(PaneRect{ 0, 0, 1100, 760 });

    const PlotGeometry magnitudeGeometry =
        rta::view::paneGeometry(axis, view.panes().magnitude, 18.0, -18.0);
    const int mTop = static_cast<int>(magnitudeGeometry.top);
    const int mBottom = static_cast<int>(magnitudeGeometry.bottom);
    const int xLow = static_cast<int>(magnitudeGeometry.xForHz(30.0));
    const int xHigh = static_cast<int>(magnitudeGeometry.xForHz(15000.0));
    const int yLow = firstLiveTraceY(image, xLow, mTop, mBottom);
    const int yHigh = firstLiveTraceY(image, xHigh, mTop, mBottom);
    REQUIRE(yLow >= 0);
    REQUIRE(yHigh >= 0);
    // The fixed fixture is flat 0 dB everywhere -- both rows land within a
    // couple of pixels of each other, unlike the MTW ramp's >20 px spread.
    CHECK(std::abs(yLow - yHigh) <= 2);

    // Phase, untouched by the magnitude-only toggle, must still read the MTW
    // fixture's flat -90 deg -- not the fixed fixture's flat 0 deg.
    const PlotGeometry phaseGeometry = rta::view::paneGeometry(axis, view.panes().phase, 180.0, -180.0);
    const int pTop = static_cast<int>(phaseGeometry.top);
    const int pBottom = static_cast<int>(phaseGeometry.bottom);
    const int xMid = static_cast<int>(phaseGeometry.xForHz(1000.0));
    const int yPhase = firstLiveTraceY(image, xMid, pTop, pBottom);
    REQUIRE(yPhase >= 0);
    const int expectedMtwY = static_cast<int>(std::lround(phaseGeometry.yForDb(-90.0)));
    CHECK(std::abs(yPhase - expectedMtwY) <= 2);
}

// CATCHES: no seam drawn at all, and a seam drawn at the wrong frequency --
// each of the six checked columns is read from the SAME mtwBands() this
// fixture built its block from, not a hand-picked pixel offset.
TEST_CASE("seam marks are drawn once per band boundary", "[transferview][mtw]") {
    const auto snap = snapshotWithBoth(4096, 48000.0);
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

    const std::vector<double> seamHz{ 187.5, 375.0, 750.0, 1500.0, 3000.0, 6000.0 };
    for (const double hz : seamHz) {
        CAPTURE(hz);
        const int x = static_cast<int>(std::lround(magnitudeGeometry.xForHz(hz)));
        bool found = false;
        for (int dx = -1; dx <= 1 && !found; ++dx) {
            for (int y = top; y < bottom; ++y) {
                if (isStoredTraceish(image.getPixelAt(x + dx, y))) {
                    found = true;
                    break;
                }
            }
        }
        CHECK(found);
    }
}
