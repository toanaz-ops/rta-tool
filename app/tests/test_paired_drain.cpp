// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests.
//
// SCOPE OF THIS FILE, stated so nobody claims more from it than it proves:
// it tests the ARITHMETIC of how far a group of rings may advance together.
// AnalysisThread::drainPaired calls the span overload (guard G3 in
// test_capture_bus_guard.cpp pins that the call is there, and
// tests_juce/test_routing_live.cpp drives it through a real bus and thread),
// so this is the arithmetic that ships. The ring plumbing around it is covered
// by those live tests, not here.
#include "measure/PairedDrain.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>

TEST_CASE("two rings advance only as far as the shorter one allows",
          "[paired-drain]") {
    // The whole point: a callback landing between two independent drains
    // leaves one ring richer, and consuming that surplus offsets the two
    // channels permanently. Taking the minimum is what refuses the surplus.
    CHECK(rta::measure::pairedHopCount(4096, 2048, 1024) == 2u);
    CHECK(rta::measure::pairedHopCount(2048, 4096, 1024) == 2u);
    CHECK(rta::measure::pairedHopCount(4096, 4096, 1024) == 4u);
}

TEST_CASE("a partial hop is left in both rings", "[paired-drain]") {
    // Whole hops only, so pushPair always sees a frame-aligned chunk -- the
    // same rule drainRole already follows for the single-channel path. A short
    // remainder stays for next time.
    CHECK(rta::measure::pairedHopCount(1500, 1500, 1024) == 1u);
    CHECK(rta::measure::pairedHopCount(1023, 5000, 1024) == 0u);
}

TEST_CASE("a degenerate hop size consumes nothing", "[paired-drain]") {
    // Divide-by-zero on the analysis thread is a hang or a crash at a show.
    CHECK(rta::measure::pairedHopCount(4096, 4096, 0) == 0u);
    CHECK(rta::measure::pairedHopCount(0, 0, 1024) == 0u);
}

TEST_CASE("a group of N rings advances only as far as its shortest", "[paired-drain]") {
    // drainPaired passes one reference plus every measurement ring routed
    // against it. The SHORTEST one decides, wherever it sits in the group.
    const std::array<std::size_t, 4> shortestLast = {8192, 6144, 7168, 2048};
    const std::array<std::size_t, 4> shortestFirst = {1024, 6144, 7168, 8192};
    const std::array<std::size_t, 4> shortestMid = {8192, 3072, 7168, 6144};
    CHECK(rta::measure::pairedHopCount(std::span<const std::size_t>(shortestLast), 1024) == 2u);
    CHECK(rta::measure::pairedHopCount(std::span<const std::size_t>(shortestFirst), 1024) == 1u);
    CHECK(rta::measure::pairedHopCount(std::span<const std::size_t>(shortestMid), 1024) == 3u);
}

TEST_CASE("a vanished ring (availability 0) holds the whole group back", "[paired-drain]") {
    // drainPaired reports a ring that bus.ring() no longer returns as 0, which
    // is how "a route lost its measurement ring" stays a no-op rather than a
    // half-aligned drain.
    const std::array<std::size_t, 3> withGap = {8192, 0, 8192};
    CHECK(rta::measure::pairedHopCount(std::span<const std::size_t>(withGap), 1024) == 0u);
}

TEST_CASE("an empty group, or a zero hop, consumes nothing", "[paired-drain]") {
    const std::array<std::size_t, 2> two = {4096, 4096};
    CHECK(rta::measure::pairedHopCount(std::span<const std::size_t>{}, 1024) == 0u);
    CHECK(rta::measure::pairedHopCount(std::span<const std::size_t>(two), 0) == 0u);
}
