// SPDX-License-Identifier: AGPL-3.0-or-later
#include "view/RepaintGate.h"

#include <catch2/catch_test_macros.hpp>

using rta::view::GateState;
using rta::view::shouldRepaint;

TEST_CASE("a new live frame repaints", "[gate]") {
    GateState g;
    CHECK(shouldRepaint(g, 1, 0));
    CHECK_FALSE(shouldRepaint(g, 1, 0));  // same frame twice: no
    CHECK(shouldRepaint(g, 2, 0));
}

TEST_CASE("a library edit repaints even with no new frame", "[gate]") {
    // The bug this fixes: renaming or hiding a stored trace produced no
    // sequence change, so the plot never redrew and the edit looked ignored.
    GateState g;
    REQUIRE(shouldRepaint(g, 5, 0));
    CHECK(shouldRepaint(g, 5, 1));
    CHECK_FALSE(shouldRepaint(g, 5, 1));
}

TEST_CASE("both moving at once is still one repaint", "[gate]") {
    GateState g;
    REQUIRE(shouldRepaint(g, 1, 1));
    CHECK_FALSE(shouldRepaint(g, 1, 1));
}

TEST_CASE("the very first call always repaints", "[gate]") {
    GateState g;
    CHECK(shouldRepaint(g, 0, 0));
    CHECK_FALSE(shouldRepaint(g, 0, 0));
}
