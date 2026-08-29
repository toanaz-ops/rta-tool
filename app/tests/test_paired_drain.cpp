// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests.
//
// SCOPE OF THIS FILE, stated so nobody claims more from it than it proves:
// it tests the ARITHMETIC of how far two rings may advance together. It does
// NOT test AnalysisThread's ring plumbing, which needs a live CaptureBus and a
// running thread and has no test target in this tree. The plumbing is reviewed
// by reading, and that is what the report must say.
#include "measure/PairedDrain.h"

#include <catch2/catch_test_macros.hpp>

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
