// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Record §7: level alignment is a per-position trim over the span the
// operator is CURRENTLY displaying, weighted by each position's own gated
// coherence -- REW's "Align SPL over a chosen span", not
// rta::dsp::spatialAverage's own u_i combine weight (see LevelAlign.h's own
// header comment for why those are two different knobs, and the choice this
// file makes between them).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "measure/LevelAlign.h"

#include <cmath>
#include <complex>
#include <vector>

using rta::measure::alignTrims;

namespace {

rta::dsp::TransferSnapshot makeSnapshot(std::vector<float> magnitudeDb, std::vector<float> coherence,
                                        double binWidthHz) {
    rta::dsp::TransferSnapshot snap;
    snap.sampleRate = 48000.0;
    snap.binWidthHz = binWidthHz;
    snap.magnitudeDb = std::move(magnitudeDb);
    snap.coherence = std::move(coherence);
    snap.h.assign(snap.magnitudeDb.size(), std::complex<double>(1.0, 0.0));
    snap.phaseRadians.assign(snap.magnitudeDb.size(), 0.0f);
    return snap;
}

}  // namespace

TEST_CASE("Three equal-trust positions align to exact closed-form trims, and re-running is a no-op",
          "[levelalign]") {
    constexpr double a = -6.0;
    constexpr double binWidthHz = 100.0;  // bins land at 0, 100, 200 Hz
    // Band [50, 250] Hz covers bins at 100 and 200 -- two bins, full trust.
    const std::vector<float> coherence{0.0f, 1.0f, 1.0f};

    std::vector<rta::dsp::TransferSnapshot> positions{
        makeSnapshot({0.0f, static_cast<float>(a), static_cast<float>(a)}, coherence, binWidthHz),
        makeSnapshot({0.0f, static_cast<float>(a + 3.0), static_cast<float>(a + 3.0)}, coherence,
                     binWidthHz),
        makeSnapshot({0.0f, static_cast<float>(a - 3.0), static_cast<float>(a - 3.0)}, coherence,
                     binWidthHz),
    };

    std::vector<double> trims{0.0, 0.0, 0.0};
    const auto moves = alignTrims(positions, 50.0, 250.0, trims);

    REQUIRE(moves.size() == 3);
    REQUIRE(moves[0].has_value());
    REQUIRE(moves[1].has_value());
    REQUIRE(moves[2].has_value());
    CHECK(*moves[0] == Catch::Approx(0.0).margin(1e-9));
    CHECK(*moves[1] == Catch::Approx(-3.0).margin(1e-9));
    CHECK(*moves[2] == Catch::Approx(3.0).margin(1e-9));
    CHECK(trims[0] == Catch::Approx(0.0).margin(1e-9));
    CHECK(trims[1] == Catch::Approx(-3.0).margin(1e-9));
    CHECK(trims[2] == Catch::Approx(3.0).margin(1e-9));

    // Re-running: every position's own weighted level now equals the
    // group's, so every move must be exactly 0 -- the alignment is a fixed
    // point, not a target that keeps sliding.
    const auto secondMoves = alignTrims(positions, 50.0, 250.0, trims);
    for (const auto& move : secondMoves) {
        REQUIRE(move.has_value());
        CHECK(*move == Catch::Approx(0.0).margin(1e-9));
    }
}

TEST_CASE("A low-trust position is weighted by its own coherence, not counted equally "
          "(the case an unweighted mean must fail)",
          "[levelalign]") {
    constexpr double a = -6.0;
    constexpr double binWidthHz = 100.0;
    const std::vector<float> fullTrust{0.0f, 1.0f, 1.0f};
    // Position 4: trust 0.1 at 100 Hz, ZERO at 200 Hz (half the span).
    const std::vector<float> halfTrust{0.0f, 0.1f, 0.0f};

    std::vector<rta::dsp::TransferSnapshot> positions{
        makeSnapshot({0.0f, static_cast<float>(a), static_cast<float>(a)}, fullTrust, binWidthHz),
        makeSnapshot({0.0f, static_cast<float>(a + 3.0), static_cast<float>(a + 3.0)}, fullTrust,
                     binWidthHz),
        makeSnapshot({0.0f, static_cast<float>(a - 3.0), static_cast<float>(a - 3.0)}, fullTrust,
                     binWidthHz),
        // An outlier reading (a + 100) at LOW trust -- an unweighted mean
        // would let it pull the target by 25 dB; the trust-weighted target
        // moves by only 10/6.1 dB.
        makeSnapshot({0.0f, static_cast<float>(a + 100.0), 0.0f}, halfTrust, binWidthHz),
    };

    std::vector<double> trims{0.0, 0.0, 0.0, 0.0};
    const auto moves = alignTrims(positions, 50.0, 250.0, trims);

    REQUIRE(moves.size() == 4);
    for (const auto& move : moves) REQUIRE(move.has_value());

    // Closed form: weightSum = 2+2+2+0.1 = 6.1; weightedSum =
    // 2a + 2(a+3) + 2(a-3) + 0.1(a+100) = 6.1a + 10 -> target = a + 10/6.1.
    const double target = a + 10.0 / 6.1;
    CHECK(*moves[0] == Catch::Approx(target - a).margin(1e-9));
    CHECK(*moves[1] == Catch::Approx(target - (a + 3.0)).margin(1e-9));
    CHECK(*moves[2] == Catch::Approx(target - (a - 3.0)).margin(1e-9));
    CHECK(*moves[3] == Catch::Approx(target - (a + 100.0)).margin(1e-9));
}

TEST_CASE("A position with zero trust across the whole span gets no trim, reported as absent",
          "[levelalign]") {
    constexpr double binWidthHz = 100.0;
    std::vector<rta::dsp::TransferSnapshot> positions{
        makeSnapshot({0.0f, -6.0f, -6.0f}, {0.0f, 1.0f, 1.0f}, binWidthHz),
        // Zero coherence everywhere in the band -- no trust at all.
        makeSnapshot({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, binWidthHz),
    };

    std::vector<double> trims{0.0, 5.0};  // position 1 starts with a pre-existing trim
    const auto moves = alignTrims(positions, 50.0, 250.0, trims);

    REQUIRE(moves.size() == 2);
    CHECK(moves[0].has_value());
    CHECK_FALSE(moves[1].has_value());
    // Untouched -- not reset to 0, not left as a 0/0 NaN.
    CHECK(trims[1] == 5.0);
}

TEST_CASE("A span containing no bin returns no moves for any position", "[levelalign]") {
    constexpr double binWidthHz = 100.0;
    std::vector<rta::dsp::TransferSnapshot> positions{
        makeSnapshot({0.0f, -6.0f, -6.0f}, {0.0f, 1.0f, 1.0f}, binWidthHz),
        makeSnapshot({0.0f, -3.0f, -3.0f}, {0.0f, 1.0f, 1.0f}, binWidthHz),
    };

    std::vector<double> trims{0.0, 0.0};
    // [10, 20] Hz contains no bin at binWidthHz = 100 Hz spacing.
    const auto moves = alignTrims(positions, 10.0, 20.0, trims);

    REQUIRE(moves.size() == 2);
    CHECK_FALSE(moves[0].has_value());
    CHECK_FALSE(moves[1].has_value());
    CHECK(trims[0] == 0.0);
    CHECK(trims[1] == 0.0);
}
