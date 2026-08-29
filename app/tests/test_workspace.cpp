// SPDX-License-Identifier: AGPL-3.0-or-later
#include "trace/Workspace.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

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
