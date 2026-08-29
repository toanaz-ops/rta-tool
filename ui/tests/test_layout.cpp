// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- ui/tests. Tests az_ui's own layout math. This file must
// contain no measurement vocabulary either: az_ui is portable, and a test that
// only makes sense for an audio analyser is a portability claim with a hole
// in it.
#include <az_ui/az_ui.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

TEST_CASE("splitVertically gives every child the full width", "[layout]") {
    const juce::Rectangle<int> area(10, 20, 300, 400);
    const std::vector<float> weights{ 1.0f, 1.0f };
    const auto rects = az::ui::splitVertically(area, weights, 0);

    REQUIRE(rects.size() == 2u);
    for (const auto& r : rects) {
        CHECK(r.getX() == area.getX());
        CHECK(r.getWidth() == area.getWidth());
    }
}

TEST_CASE("splitVertically divides height in proportion to the weights",
          "[layout]") {
    // 3:1 over 400 px with no gap -> 300 and 100. Chosen so the exact answer
    // is an integer and the assertion needs no tolerance to hide a rounding
    // bug behind.
    const juce::Rectangle<int> area(0, 0, 100, 400);
    const std::vector<float> weights{ 3.0f, 1.0f };
    const auto rects = az::ui::splitVertically(area, weights, 0);

    REQUIRE(rects.size() == 2u);
    CHECK(rects[0].getHeight() == 300);
    CHECK(rects[1].getHeight() == 100);
}

TEST_CASE("splitVertically consumes the area exactly, gaps included",
          "[layout]") {
    // The property that actually matters on screen: no pixel row is left
    // unpainted between two panes, and no pane overhangs the bottom. Asserted
    // over heights that do NOT divide evenly, which is where a naive
    // round-each-independently implementation leaks a row.
    //
    // The origin is deliberately NOT (0, 0). With it at the origin, every
    // assertion below holds for an implementation that ignores area.getY()
    // entirely and stacks from zero -- the whole vertical placement of the
    // split would be asserted by nothing. A nonzero y costs one character and
    // makes every assertion in this case cover the origin too.
    for (const int height : { 199, 200, 201, 333, 761 }) {
        const juce::Rectangle<int> area(7, 13, 100, height);
        const std::vector<float> weights{ 5.0f, 3.0f, 2.0f };
        const int gap = 4;
        const auto rects = az::ui::splitVertically(area, weights, gap);

        REQUIRE(rects.size() == 3u);
        CHECK(rects.front().getY() == area.getY());
        CHECK(rects.back().getBottom() == area.getBottom());
        for (std::size_t i = 1; i < rects.size(); ++i) {
            CHECK(rects[i].getY() == rects[i - 1].getBottom() + gap);
        }
    }
}

TEST_CASE("splitVertically refuses to invent a pane out of nothing",
          "[layout]") {
    const juce::Rectangle<int> area(0, 0, 100, 400);

    CHECK(az::ui::splitVertically(area, std::vector<float>{}, 0).empty());

    // A non-positive weight is not a caller error worth dropping a pane over:
    // the caller asked for N children and must get N back, or its own indexing
    // into the result silently shifts by one. A zero-height child is a visible
    // nothing; a missing child is a wrong arrangement.
    //
    // NEGATIVE, not just zero: the implementation clamps with
    // std::max(0.0f, w), and with only a 0.0f in the vector that clamp can be
    // deleted with every test still green. A negative weight is also what a
    // normalised-on-read layout file can actually deliver.
    const std::vector<float> weights{ 1.0f, -1.0f, 1.0f };
    const auto rects = az::ui::splitVertically(area, weights, 0);
    REQUIRE(rects.size() == 3u);
    CHECK(rects[1].getHeight() == 0);
    // The two real panes still split the whole area between them -- a negative
    // weight must not leak height out of the total.
    CHECK(rects[0].getHeight() + rects[2].getHeight() == area.getHeight());

    // Every weight non-positive: nobody expressed a preference, so equal
    // shares. Untested, this branch is a comment with an implementation
    // attached.
    const std::vector<float> noPreference{ 0.0f, 0.0f };
    const auto equal = az::ui::splitVertically(area, noPreference, 0);
    REQUIRE(equal.size() == 2u);
    CHECK(equal[0].getHeight() == equal[1].getHeight());
    CHECK(equal[0].getHeight() + equal[1].getHeight() == area.getHeight());

    // Area shorter than the gaps alone: every child clamps to zero height and
    // none goes negative. juce::Rectangle happily holds a negative height and
    // draws nothing, so this would be invisible until a child component's own
    // resized() divided by it.
    const juce::Rectangle<int> tiny(0, 0, 100, 2);
    const std::vector<float> three{ 1.0f, 1.0f, 1.0f };
    for (const auto& r : az::ui::splitVertically(tiny, three, 10)) {
        CHECK(r.getHeight() >= 0);
    }
}
