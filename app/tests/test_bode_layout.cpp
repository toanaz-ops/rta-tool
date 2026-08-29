// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests. Decisions 1 and 2 of
// docs/dsp/2026-08-29-display-layer-l5c.md.
#include "view/BodeLayout.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using rta::view::BodePanes;
using rta::view::PaneRect;

namespace {
constexpr int kGap = 6;
}

TEST_CASE("the coherence ribbon keeps a fixed height at every window size",
          "[bode-layout]") {
    // Decision 2: the ribbon is furniture, like an axis strip. Scaling it with
    // the window buys nothing, so it is the ONE pane whose height is a
    // constant -- and the mockup's mistake was making the magnitude pane one
    // too.
    for (const int height : { 300, 500, 760, 1200, 2000 }) {
        const PaneRect content{ 0, 0, 1000, height };
        const BodePanes panes = rta::view::bodePanes(content, kGap);
        CHECK(panes.ribbon.height == rta::view::kRibbonHeight);
    }
}

TEST_CASE("magnitude and phase split the remaining height 5:3", "[bode-layout]") {
    // The RULE, not the outcome at one size. The mockup's fixed 380 px
    // magnitude pane happened to give ~62/38 at 1100x760 and collapsed the
    // phase pane at any shorter height; this asserts the proportion holds
    // across a 6x range of window heights.
    for (const int height : { 300, 307, 500, 760, 1200, 2000 }) {
        const PaneRect content{ 0, 0, 1000, height };
        const BodePanes panes = rta::view::bodePanes(content, kGap);

        REQUIRE(panes.magnitude.height > 0);
        REQUIRE(panes.phase.height > 0);

        // 5:3 as an integer cross-multiplication rather than a float ratio
        // with a loose epsilon, which would pass for 6:3 at small heights.
        //
        // The bound is 7, and that number is derived, not chosen. With
        // magnitude = floor(5a/8) and phase = a - magnitude over an available
        // height a:
        //     cross = 3*magnitude - 5*phase = 8*floor(5a/8) - 5a = -(5a mod 8)
        // which ranges over 0..-7. A CORRECT implementation therefore produces
        // |cross| = 7 whenever 5a is 1 (mod 8). A tighter bound is not a
        // stricter test, it is a test that fails on correct code at heights
        // nobody happened to pick -- an earlier draft used 5, which survives
        // only because these five heights all land on residues {0, 4}.
        //
        // Discrimination is unaffected: a 6:3 split gives |cross| near 76 at
        // height 300, two orders away from the bound.
        const int cross = panes.magnitude.height * 3 - panes.phase.height * 5;
        CHECK(std::abs(cross) <= 7);
    }
}

TEST_CASE("panes stack without overlapping and stay inside the content",
          "[bode-layout]") {
    for (const int height : { 300, 500, 760, 1200 }) {
        const PaneRect content{ 12, 34, 1000, height };
        const BodePanes panes = rta::view::bodePanes(content, kGap);

        CHECK(panes.ribbon.y >= content.y);
        CHECK(panes.magnitude.y >= panes.ribbon.y + panes.ribbon.height);
        CHECK(panes.phase.y >= panes.magnitude.y + panes.magnitude.height);
        // The frequency label strip belongs to the bottom pane and is reserved
        // BELOW the phase pane's plot area -- so the phase pane's own bottom
        // must clear the content bottom by at least that strip.
        CHECK(panes.phase.y + panes.phase.height
              <= content.y + content.height - rta::view::kFrequencyLabelHeight);
    }
}

TEST_CASE("a window too short for the furniture yields no negative pane",
          "[bode-layout]") {
    // A negative height is not caught by drawing -- juce::Rectangle holds it
    // and paints nothing -- so it surfaces later, as a divide-by-zero inside
    // whatever computes a dB-per-pixel scale. Catch it here.
    for (const int height : { 0, 10, 40, 60, 80 }) {
        const PaneRect content{ 0, 0, 1000, height };
        const BodePanes panes = rta::view::bodePanes(content, kGap);
        CHECK(panes.ribbon.height >= 0);
        CHECK(panes.magnitude.height >= 0);
        CHECK(panes.phase.height >= 0);
        // The ribbon is a FIXED height, which at these sizes means it must
        // still be clamped to what the content actually has. Without this,
        // `std::min(kRibbonHeight, ...)` can be deleted with every other
        // assertion here still green -- an unclamped 34 satisfies `>= 0` just
        // as happily as a correct 10 does.
        CHECK(panes.ribbon.height <= content.height);
    }
}

TEST_CASE("every pane maps frequency to the same pixel column", "[bode-layout]") {
    // Decision 1's load-bearing half. Stacking costs half the vertical
    // resolution per quantity, and the ONLY thing that buys back is a dip and
    // its phase swing landing on the same pixel column. Panes with
    // independently computed x mappings that disagree by a pixel would destroy
    // that silently -- nothing on screen would look wrong.
    const PaneRect content{ 12, 34, 1000, 760 };
    const BodePanes panes = rta::view::bodePanes(content, kGap);
    const auto axis = rta::view::frequencyAxis(content);

    const auto ribbon = rta::view::paneGeometry(axis, panes.ribbon, 1.0, 0.0);
    const auto magnitude = rta::view::paneGeometry(axis, panes.magnitude, 18.0, -18.0);
    const auto phase = rta::view::paneGeometry(axis, panes.phase, 180.0, -180.0);

    for (const double hz : { 20.0, 100.0, 1000.0, 2000.0, 20000.0 }) {
        CHECK(ribbon.xForHz(hz) == magnitude.xForHz(hz));
        CHECK(magnitude.xForHz(hz) == phase.xForHz(hz));
    }
}

TEST_CASE("paneGeometry takes its x mapping from the axis, never from the pane",
          "[bode-layout]") {
    // The mutation this exists to kill: deriving left/right from the pane
    // rectangle instead of the shared axis. That passes every assertion above
    // today, because all three panes currently share the content's x and
    // width -- and breaks the moment any pane gains an inset. Feed a pane a
    // deliberately different x and width and assert the mapping does not move.
    const PaneRect content{ 12, 34, 1000, 760 };
    const auto axis = rta::view::frequencyAxis(content);

    const PaneRect normal{ 12, 100, 1000, 200 };
    const PaneRect inset{ 212, 100, 600, 200 };

    const auto a = rta::view::paneGeometry(axis, normal, 18.0, -18.0);
    const auto b = rta::view::paneGeometry(axis, inset, 18.0, -18.0);

    CHECK(a.left == b.left);
    CHECK(a.right == b.right);

    // The pane still owns its own VERTICAL extent -- that is what a pane is.
    // Both panes above sit at the same y on purpose, so the x assertions are
    // the only thing the inset can move; a third pane at a different y proves
    // the y half is still read from the pane and not from the axis.
    const PaneRect lower{ 12, 400, 1000, 200 };
    const auto c = rta::view::paneGeometry(axis, lower, 18.0, -18.0);
    CHECK(c.top == static_cast<float>(lower.y));
    CHECK(c.bottom == static_cast<float>(lower.y + lower.height));
    CHECK(c.left == a.left);
}

TEST_CASE("the dB range belongs to the pane, not the axis", "[bode-layout]") {
    const PaneRect content{ 0, 0, 1000, 760 };
    const auto axis = rta::view::frequencyAxis(content);
    const PaneRect pane{ 0, 100, 1000, 200 };

    const auto magnitude = rta::view::paneGeometry(axis, pane, 18.0, -18.0);
    const auto phase = rta::view::paneGeometry(axis, pane, 180.0, -180.0);

    CHECK(magnitude.dbTop == Catch::Approx(18.0));
    CHECK(phase.dbTop == Catch::Approx(180.0));
    // Whole-degree readouts (project CLAUDE.md): +-180 with 90-degree ticks
    // divides exactly, so the axis never needs a fractional label.
    CHECK(phase.yForDb(0.0) == Catch::Approx((phase.top + phase.bottom) / 2.0f));
}
