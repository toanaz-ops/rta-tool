// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rtatool_view_tests. The per-plot MTW/Fixed UI control
// docs/reports/005-mtw-engine.md "Known gaps" names as missing: "the
// per-plot source toggle has no UI control yet -- it exists as an API the
// tests and a future control can call." This file exercises the CONTROL,
// not the API `test_transfer_view_mtw.cpp` already covers directly through
// `setSource` -- every check below goes through a real button click
// (`TransferSourceToggle::clickMtwButton`/`clickFixedButton`), which is what
// actually catches a control built but never wired to the setter.
#include <catch2/catch_test_macros.hpp>

#include "test_transfer_view_helpers.h"

#include "measure/SyntheticSnapshot.h"
#include "view/BodeLayout.h"
#include "view/PlotGeometry.h"
#include "view/TransferSourceToggle.h"

#include <vector>

using rta::measure::Snapshot;
using rta::measure::SnapshotPtr;
using rta::measure::StaticSnapshotSource;
using rta::view::FrequencyAxis;
using rta::view::PaneRect;
using rta::view::PlotGeometry;
using rta::view::TransferPane;
using rta::view::TransferSource;
using rta::view::TransferSourceToggle;
using rta::view::TransferView;

namespace {

/// A snapshot carrying both blocks -- makeSyntheticMtw/makeSyntheticTransfer
/// (Task 1's own fixture pair) rather than a hand-built one, so this file
/// adds no second copy of "what an MTW block looks like" for a fixed engine
/// pane to fall back to be visibly different from.
SnapshotPtr snapshotWithBoth(std::size_t fftSize, double sampleRate) {
    auto snap = std::make_shared<Snapshot>();
    snap->sequence = 1;
    snap->sampleRate = sampleRate;
    snap->fftSize = fftSize;
    snap->transfer = rta::measure::makeSyntheticTransfer(fftSize, sampleRate, 18);
    snap->mtw = rta::measure::makeSyntheticMtw();
    return snap;
}

/// True if any pixel in `[x-1, x+1] x [geometry.top, geometry.bottom)` reads
/// as the seam/readout colour -- the same predicate and margin
/// test_transfer_view_mtw.cpp's own seam tests use.
bool seamPixelFound(const juce::Image& image, const PlotGeometry& geometry, double hz) {
    const int x = static_cast<int>(std::lround(geometry.xForHz(hz)));
    const int top = static_cast<int>(geometry.top);
    const int bottom = static_cast<int>(geometry.bottom);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int y = top; y < bottom; ++y) {
            if (isStoredTraceish(image.getPixelAt(x + dx, y))) return true;
        }
    }
    return false;
}

/// The six interior seam frequencies the default MtwConfig produces -- see
/// test_transfer_view_mtw.cpp's identically named fixture for why these
/// particular six numbers and not some other set.
const std::vector<double>& defaultSeamHz() {
    static const std::vector<double> seams{ 187.5, 375.0, 750.0, 1500.0, 3000.0, 6000.0 };
    return seams;
}

}  // namespace

// CATCHES: a control built but never added to the view (a null or dangling
// toggle), and a default that shows FIXED pressed when the view's own
// default source is MTW (TransferView.h's sources_ default).
TEST_CASE("every pane's source toggle exists and defaults to MTW", "[transferview][sourcetoggle]") {
    const auto snap = snapshotWithBoth(4096, 48000.0);
    const StaticSnapshotSource source(snap);
    TransferView view(source);
    view.setSize(1100, 760);
    view.resized();

    CHECK(view.sourceToggle(TransferPane::Magnitude).isMtwSelected());
    CHECK(view.sourceToggle(TransferPane::Phase).isMtwSelected());
    CHECK(view.sourceToggle(TransferPane::Coherence).isMtwSelected());
}

// CATCHES: a button wired to nothing, or wired to the wrong pane's setter --
// clicking the magnitude toggle's FIXED button must change ONLY
// view.source(Magnitude), the same isolation test_transfer_view_mtw.cpp's
// direct-setSource version already proves for the API this control calls.
TEST_CASE("clicking a toggle's FIXED button switches only that pane's reported source",
          "[transferview][sourcetoggle]") {
    const auto snap = snapshotWithBoth(4096, 48000.0);
    const StaticSnapshotSource source(snap);
    TransferView view(source);
    view.setSize(1100, 760);
    view.resized();

    view.sourceToggle(TransferPane::Magnitude).clickFixedButton();

    CHECK(view.source(TransferPane::Magnitude) == TransferSource::Fixed);
    CHECK(view.source(TransferPane::Phase) == TransferSource::Mtw);
    CHECK(view.source(TransferPane::Coherence) == TransferSource::Mtw);
    CHECK_FALSE(view.sourceToggle(TransferPane::Magnitude).isMtwSelected());
    CHECK(view.sourceToggle(TransferPane::Phase).isMtwSelected());

    // Clicking MTW back must be just as wired.
    view.sourceToggle(TransferPane::Magnitude).clickMtwButton();
    CHECK(view.source(TransferPane::Magnitude) == TransferSource::Mtw);
    CHECK(view.sourceToggle(TransferPane::Magnitude).isMtwSelected());
}

// The negative test the L3 verifier's own "seam marks are absent from every
// pane once its source is fixed" (test_transfer_view_mtw.cpp) already
// checks through `setSource` directly -- mirrored here through the actual
// button clicks a user would perform, so a control that LOOKS wired but
// isn't actually connected to `setSource` (e.g. a button that only updates
// its own pressed state) is caught by the picture, not just by
// `view.source()`'s own return value above.
// CATCHES: `onClick` calling `refresh()` without first calling
// `view.setSource` -- the toggle would show FIXED pressed while the view
// still draws MTW's seams.
TEST_CASE("clicking FIXED on every toggle removes every seam mark from the rendered view",
          "[transferview][sourcetoggle]") {
    const auto snap = snapshotWithBoth(4096, 48000.0);
    const StaticSnapshotSource source(snap);
    TransferView view(source);
    view.setSize(1100, 760);
    view.resized();

    view.sourceToggle(TransferPane::Magnitude).clickFixedButton();
    view.sourceToggle(TransferPane::Phase).clickFixedButton();
    view.sourceToggle(TransferPane::Coherence).clickFixedButton();

    const auto image = renderView(view, 1100, 760);
    const FrequencyAxis axis = rta::view::frequencyAxis(PaneRect{ 0, 0, 1100, 760 });
    const PlotGeometry magnitudeGeometry =
        rta::view::paneGeometry(axis, view.panes().magnitude, 18.0, -18.0);
    const PlotGeometry phaseGeometry =
        rta::view::paneGeometry(axis, view.panes().phase, 180.0, -180.0);
    const PlotGeometry ribbonGeometry = rta::view::paneGeometry(axis, view.panes().ribbon, 1.0, 0.0);

    for (const double hz : defaultSeamHz()) {
        CAPTURE(hz);
        CHECK_FALSE(seamPixelFound(image, magnitudeGeometry, hz));
        CHECK_FALSE(seamPixelFound(image, phaseGeometry, hz));
        CHECK_FALSE(seamPixelFound(image, ribbonGeometry, hz));
    }
}
