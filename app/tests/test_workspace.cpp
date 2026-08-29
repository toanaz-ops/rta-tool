// SPDX-License-Identifier: AGPL-3.0-or-later
#include "trace/Workspace.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <limits>

using namespace rta::trace;
using Catch::Approx;

TEST_CASE("weights are normalised on read", "[workspace]") {
    // {2, 2} and {0.5, 0.5} both express "equal preference" but at different
    // scales -- if normalisePanes just divided by kMaxPanes, or forgot to
    // divide at all, one of these two would come out wrong while the other
    // (by coincidence, since 0.5+0.5 already sums to 1) would still pass.
    std::vector<PaneSpec> doubled{PaneSpec{"rta", 2.0f}, PaneSpec{"rta", 2.0f}};
    auto a = normalisePanes(doubled);
    REQUIRE(a.size() == 2u);
    CHECK(a[0].weight == Approx(0.5f));
    CHECK(a[1].weight == Approx(0.5f));

    std::vector<PaneSpec> alreadySumming{PaneSpec{"rta", 0.5f}, PaneSpec{"rta", 0.5f}};
    auto b = normalisePanes(alreadySumming);
    REQUIRE(b.size() == 2u);
    CHECK(b[0].weight == Approx(0.5f));
    CHECK(b[1].weight == Approx(0.5f));
}

TEST_CASE("a collapsed pane stays collapsed, at any scale", "[workspace]") {
    // {1, 1, 0} and {2, 2, 0} express the IDENTICAL preference ("these two
    // share the space equally, this one is collapsed"), just written at
    // different scales. Both must yield {0.5, 0.5, 0.0} -- two load-bearing
    // assertions per input: the trailing 0.0 pins that a non-positive weight
    // is NOT redistributed into an equal share (the user collapsed that
    // pane on purpose), and comparing the two scales pins scale-invariance.
    // A joint-normalisation implementation (replace zero with 1/n, THEN
    // normalise the whole set together) passes every all-positive test in
    // this file and fails only this one: {1,1,0} -> {1,1,1/3} -> sum 7/3 ->
    // {3/7, 3/7, 1/7} (collapsed pane gets 0.143), while {2,2,0} ->
    // {2,2,1/3} -> sum 13/3 -> {6/13, 6/13, 1/13} (0.077) -- two different
    // layouts for the same stated preference.
    std::vector<PaneSpec> small{PaneSpec{"rta", 1.0f}, PaneSpec{"rta", 1.0f}, PaneSpec{"rta", 0.0f}};
    auto a = normalisePanes(small);
    REQUIRE(a.size() == 3u);
    CHECK(a[0].weight == Approx(0.5f));
    CHECK(a[1].weight == Approx(0.5f));
    CHECK(a[2].weight == Approx(0.0f));

    std::vector<PaneSpec> scaled{PaneSpec{"rta", 2.0f}, PaneSpec{"rta", 2.0f}, PaneSpec{"rta", 0.0f}};
    auto b = normalisePanes(scaled);
    REQUIRE(b.size() == 3u);
    CHECK(b[0].weight == Approx(0.5f));
    CHECK(b[1].weight == Approx(0.5f));
    CHECK(b[2].weight == Approx(0.0f));

    // The negative case: a single positive survivor takes the whole share,
    // and the negative pane -- like the zero above -- stays at exactly 0,
    // not floored-then-redistributed.
    std::vector<PaneSpec> withNegative{PaneSpec{"rta", 1.0f}, PaneSpec{"rta", 0.0f}, PaneSpec{"rta", -1.0f}};
    auto c = normalisePanes(withNegative);
    REQUIRE(c.size() == 3u);
    CHECK(c[0].weight == Approx(1.0f));
    CHECK(c[1].weight == Approx(0.0f));
    CHECK(c[2].weight == Approx(0.0f));

    // inf is not a pane that swallows the whole window -- std::from_chars
    // parses "inf" from a hand-edited file without error, and a
    // sum-then-divide implementation that does not special-case it would
    // either give this pane the entire 1.0 share or turn every weight into
    // NaN. It must be treated exactly like the negative case above.
    std::vector<PaneSpec> withInf{
        PaneSpec{"rta", 1.0f}, PaneSpec{"rta", 0.0f}, PaneSpec{"rta", std::numeric_limits<float>::infinity()}};
    auto d = normalisePanes(withInf);
    REQUIRE(d.size() == 3u);
    CHECK(d[0].weight == Approx(1.0f));
    CHECK(d[1].weight == Approx(0.0f));
    CHECK(d[2].weight == Approx(0.0f));
}

TEST_CASE("more than three panes are clamped", "[workspace]") {
    // Decision 6 caps at 3, matching Open Sound Meter's: below roughly a
    // third of a 760 px window a dB pane stops resolving the 0.1 dB the
    // readout rules promise. Five specs in, three out -- and the survivors
    // must still sum to 1, not just number kMaxPanes.
    std::vector<PaneSpec> five{
        PaneSpec{"rta", 1.0f}, PaneSpec{"transfer", 1.0f}, PaneSpec{"rta", 1.0f},
        PaneSpec{"rta", 1.0f}, PaneSpec{"rta", 1.0f},
    };
    auto out = normalisePanes(five);
    REQUIRE(out.size() == static_cast<std::size_t>(kMaxPanes));
    CHECK(out[0].view == "rta");
    CHECK(out[1].view == "transfer");
    CHECK(out[2].view == "rta");
    float sum = 0.0f;
    for (const auto& p : out) sum += p.weight;
    CHECK(sum == Approx(1.0f));
}

TEST_CASE("an all-zero weight set becomes equal shares", "[workspace]") {
    // Nobody expressed a preference; privileging pane 0 for no stated reason
    // would be inventing one. An implementation that left a zero weight as
    // zero (dividing only by the positive-weight sum, which would be zero
    // here and undefined) or that gave pane 0 the whole 1.0 share would both
    // fail this.
    std::vector<PaneSpec> zeros{PaneSpec{"rta", 0.0f}, PaneSpec{"transfer", 0.0f}, PaneSpec{"rta", 0.0f}};
    auto out = normalisePanes(zeros);
    REQUIRE(out.size() == 3u);
    for (const auto& p : out) CHECK(p.weight == Approx(1.0f / 3.0f));
}

TEST_CASE("an empty workspace is one rta pane", "[workspace]") {
    // A session saved before workspaces existed, and a brand-new session,
    // decode to the same empty pane list and must both open the same way: to
    // exactly one default pane, not to zero panes (which would leave nothing
    // for app/ to draw) and not to a pane with a weight that isn't 1.
    auto out = normalisePanes({});
    REQUIRE(out.size() == 1u);
    CHECK(out[0].view == kDefaultPaneView);
    CHECK(out[0].weight == Approx(1.0f));
}
