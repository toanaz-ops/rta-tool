// SPDX-License-Identifier: AGPL-3.0-or-later
//
// SpatialAverage is arithmetic over hand-built TransferSnapshots, so every
// expected value here is a closed form from
// docs/dsp/2026-09-06-multichannel-l6b.md §2-§5 and the plan's task A1 table,
// never "whatever the code printed". Test files sit outside
// check_coherence_gate.cmake's glob (its own header note), so a fixture may
// assign `.coherence` directly to build a gate-passed position without an
// engine in the path.
//
// Tolerance note: result fields are `std::vector<float>`, so a double closed
// form cast into them picks up float32's ~1.19e-7 relative rounding
// (memory/float32-fft-precision.md) even though no RealFft sits in this
// path -- the storage itself is the lossy step. `epsilon(1e-6)` gives ~8x
// headroom over that floor for nonzero values; near-zero cancellations
// (opposition, three-way phase cancellation) use the plan's own tiny
// absolute margins, since those residuals are themselves the quantity being
// checked and float32 preserves their relative precision fine at that scale.

#include "rta/dsp/SpatialAverage.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>
#include <vector>

using namespace rta::dsp;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr double kBinWidthHz = 46.875;

/// A hand-built, gate-passed position: the same |H| (given as a dB level),
/// phase and coherence at every bin.
TransferSnapshot fixture(double levelDb, double phaseRadians, double gamma2, std::size_t bins) {
    TransferSnapshot snap;
    snap.sampleRate = kSampleRate;
    snap.binWidthHz = kBinWidthHz;
    const double magnitude = std::pow(10.0, levelDb / 20.0);
    snap.h.assign(bins, std::polar(magnitude, phaseRadians));
    snap.magnitudeDb.assign(bins, static_cast<float>(levelDb));
    snap.phaseRadians.assign(bins, static_cast<float>(phaseRadians));
    snap.coherence = std::vector<float>(bins, static_cast<float>(gamma2));
    return snap;
}

/// Same, but ungated -- coherence stays nullopt, as makeSnapshot() leaves it
/// below minimumEffectiveAverages.
TransferSnapshot ungated(double levelDb, std::size_t bins) {
    TransferSnapshot snap;
    snap.sampleRate = kSampleRate;
    snap.binWidthHz = kBinWidthHz;
    const double magnitude = std::pow(10.0, levelDb / 20.0);
    snap.h.assign(bins, std::complex<double>(magnitude, 0.0));
    snap.magnitudeDb.assign(bins, static_cast<float>(levelDb));
    snap.phaseRadians.assign(bins, 0.0f);
    return snap;  // coherence left nullopt
}

/// Record §2's dB-mean closed form: L = a + c*delta/(1+c) for two positions,
/// coherence 1 and c, equal trims.
double closedFormDbMean(double a, double c, double delta) { return a + c * delta / (1.0 + c); }

/// Record §2's power-mode closed form for the same two positions.
double closedFormPowerDb(double a, double c, double delta) {
    return 10.0 * std::log10((std::pow(10.0, a / 10.0) + c * std::pow(10.0, (a + delta) / 10.0)) /
                              (1.0 + c));
}

}  // namespace

TEST_CASE("N copies of one snapshot average to the input", "[spatial][identity]") {
    for (const std::size_t n : {std::size_t{1}, std::size_t{2}, std::size_t{5}}) {
        CAPTURE(n);
        constexpr std::size_t kBins = 4;
        const auto one = fixture(-6.0, 0.7, 0.9, kBins);
        const std::vector<TransferSnapshot> positions(n, one);
        const std::vector<double> u(n, 1.0);

        const auto result = spatialAverage(positions, u);
        REQUIRE(result.has_value());
        for (std::size_t k = 0; k < kBins; ++k) {
            CAPTURE(k);
            REQUIRE(result->bins[k].absence == SpatialAbsence::Present);
            REQUIRE(result->bins[k].contributors == n);
            REQUIRE(result->magnitudeDb[k] == Catch::Approx(-6.0).epsilon(1e-6));
            REQUIRE(result->phaseRadians[k] == Catch::Approx(0.7).epsilon(1e-6));
            REQUIRE(result->phaseAgreement[k] == Catch::Approx(1.0).epsilon(1e-6));
        }
    }
}

TEST_CASE("H and its negation keep the dB mean but lose the phase", "[spatial][opposition]") {
    constexpr double kLevel = -3.0;
    constexpr double kGamma2 = 0.8;
    constexpr std::size_t kBins = 2;

    const auto positionA = fixture(kLevel, 0.0, kGamma2, kBins);
    TransferSnapshot positionB = positionA;
    for (auto& h : positionB.h) h = -h;  // bit-exact negation, never re-derived from level/phase
    for (std::size_t k = 0; k < kBins; ++k) {
        positionB.phaseRadians[k] = static_cast<float>(std::arg(positionB.h[k]));
    }

    // A naive complex (vector) mean of H and -H is exactly zero -- the
    // failure mode §2 argues against -- which is why the combine works from
    // |H_i| bin by bin instead.
    for (std::size_t k = 0; k < kBins; ++k) {
        const std::complex<double> vectorMean = (positionA.h[k] + positionB.h[k]) / 2.0;
        REQUIRE(std::abs(vectorMean) == 0.0);
    }

    const std::vector<TransferSnapshot> positions{positionA, positionB};
    const std::vector<double> u{1.0, 1.0};
    const auto result = spatialAverage(positions, u);
    REQUIRE(result.has_value());

    for (std::size_t k = 0; k < kBins; ++k) {
        CAPTURE(k);
        REQUIRE(result->bins[k].absence == SpatialAbsence::Present);
        REQUIRE(result->bins[k].contributors == 2);
        REQUIRE(result->magnitudeDb[k] == Catch::Approx(kLevel).epsilon(1e-6));
        REQUIRE(result->phaseAgreement[k] == Catch::Approx(0.0).margin(3e-16));
        REQUIRE_FALSE(result->bins[k].phasePresent);  // R3's floor at 2 contributors
    }
}

TEST_CASE("the weight is trim times gated coherence", "[spatial][weight]") {
    constexpr double kA = -6.0;
    constexpr double kDelta = 4.0;
    constexpr std::size_t kBins = 4;

    SECTION("c = 0.5, equal trims") {
        const double expected = closedFormDbMean(kA, 0.5, kDelta);
        REQUIRE(expected == Catch::Approx(-4.666666666666667).margin(1e-9));

        const std::vector<TransferSnapshot> positions{fixture(kA, 0.0, 1.0, kBins),
                                                        fixture(kA + kDelta, 0.0, 0.5, kBins)};
        const std::vector<double> u{1.0, 1.0};
        const auto result = spatialAverage(positions, u);
        REQUIRE(result.has_value());
        for (std::size_t k = 0; k < kBins; ++k) {
            REQUIRE(result->magnitudeDb[k] == Catch::Approx(expected).epsilon(1e-6));
            REQUIRE(result->bins[k].contributors == 2);
        }
    }

    SECTION("u_B = 0 mutes B entirely (R4: excluded, not zero-weighted)") {
        const std::vector<TransferSnapshot> positions{fixture(kA, 0.0, 1.0, kBins),
                                                        fixture(kA + kDelta, 0.0, 0.5, kBins)};
        const std::vector<double> u{1.0, 0.0};
        const auto result = spatialAverage(positions, u);
        REQUIRE(result.has_value());
        for (std::size_t k = 0; k < kBins; ++k) {
            REQUIRE(result->magnitudeDb[k] == Catch::Approx(kA).epsilon(1e-6));
            REQUIRE(result->bins[k].contributors == 1);
        }
    }

    SECTION("c = 1, equal trims") {
        const double expected = kA + kDelta / 2.0;
        REQUIRE(expected == Catch::Approx(-4.0).margin(1e-9));

        const std::vector<TransferSnapshot> positions{fixture(kA, 0.0, 1.0, kBins),
                                                        fixture(kA + kDelta, 0.0, 1.0, kBins)};
        const std::vector<double> u{1.0, 1.0};
        const auto result = spatialAverage(positions, u);
        REQUIRE(result.has_value());
        for (std::size_t k = 0; k < kBins; ++k) {
            REQUIRE(result->magnitudeDb[k] == Catch::Approx(expected).epsilon(1e-6));
        }
    }
}

TEST_CASE("power differs from dB by the SMPTE clause's own numbers", "[spatial][power]") {
    constexpr std::size_t kBins = 2;

    // The record's headline literal, and the gap over the dB mean, at a=0,c=1.
    REQUIRE(closedFormPowerDb(0.0, 1.0, 4.0) == Catch::Approx(2.4451046744531246).margin(1e-9));
    REQUIRE(closedFormPowerDb(0.0, 1.0, 4.0) - closedFormDbMean(0.0, 1.0, 4.0) ==
            Catch::Approx(0.4451046744531246).margin(1e-9));

    for (const double delta : {2.0, 3.0, 4.0}) {
        CAPTURE(delta);
        const double expected = closedFormPowerDb(0.0, 1.0, delta);
        const std::vector<TransferSnapshot> positions{fixture(0.0, 0.0, 1.0, kBins),
                                                        fixture(delta, 0.0, 1.0, kBins)};
        const std::vector<double> u{1.0, 1.0};
        const auto result = spatialAverage(positions, u, SpatialMode::Power);
        REQUIRE(result.has_value());
        for (std::size_t k = 0; k < kBins; ++k) {
            REQUIRE(result->magnitudeDb[k] == Catch::Approx(expected).epsilon(1e-6));
        }
    }

    {
        const double expected = closedFormPowerDb(-6.0, 0.5, 4.0);
        REQUIRE(expected == Catch::Approx(-4.2276309521366855).margin(1e-9));
        const std::vector<TransferSnapshot> positions{fixture(-6.0, 0.0, 1.0, kBins),
                                                        fixture(-2.0, 0.0, 0.5, kBins)};
        const std::vector<double> u{1.0, 1.0};
        const auto result = spatialAverage(positions, u, SpatialMode::Power);
        REQUIRE(result.has_value());
        for (std::size_t k = 0; k < kBins; ++k) {
            REQUIRE(result->magnitudeDb[k] == Catch::Approx(expected).epsilon(1e-6));
        }
    }
}

TEST_CASE("an ungated position is excluded", "[spatial][gate]") {
    constexpr std::size_t kBins = 3;
    const auto a = fixture(-3.0, 0.0, 1.0, kBins);
    const auto b = fixture(-3.0, 0.0, 1.0, kBins);
    const auto c = ungated(-3.0, kBins);

    const std::vector<TransferSnapshot> positions{a, b, c};
    const std::vector<double> u{1.0, 1.0, 1.0};
    const auto result = spatialAverage(positions, u);
    REQUIRE(result.has_value());
    for (std::size_t k = 0; k < kBins; ++k) {
        CAPTURE(k);
        REQUIRE(result->bins[k].contributors == 2);
        REQUIRE(result->bins[k].absence == SpatialAbsence::Present);
        REQUIRE(result->magnitudeDb[k] == Catch::Approx(-3.0).epsilon(1e-6));
    }

    const std::vector<TransferSnapshot> allUngated{ungated(-3.0, kBins), ungated(-3.0, kBins),
                                                     ungated(-3.0, kBins)};
    REQUIRE_FALSE(spatialAverage(allUngated, u).has_value());
}

TEST_CASE("zero weight is a different absence than zero contributors", "[spatial][absence]") {
    constexpr std::size_t kBins = 8;
    constexpr std::size_t kZeroWeightBin = 7;

    TransferSnapshot a = fixture(-3.0, 0.3, 0.9, kBins);
    TransferSnapshot b = fixture(-1.0, -0.2, 0.9, kBins);
    (*a.coherence)[kZeroWeightBin] = 0.0f;
    (*b.coherence)[kZeroWeightBin] = 0.0f;

    const std::vector<TransferSnapshot> positions{a, b};
    const std::vector<double> u{1.0, 1.0};
    const auto result = spatialAverage(positions, u);
    REQUIRE(result.has_value());

    for (std::size_t k = 0; k < kBins; ++k) {
        CAPTURE(k);
        REQUIRE(result->bins[k].contributors == 2);
        REQUIRE(result->bins[k].absence == (k == kZeroWeightBin ? SpatialAbsence::NoWeight
                                                                 : SpatialAbsence::Present));
        REQUIRE(std::isfinite(result->magnitudeDb[k]));
        REQUIRE(std::isfinite(result->phaseRadians[k]));
        REQUIRE(std::isfinite(result->phaseAgreement[k]));
        REQUIRE(std::isfinite(result->weightedCoherence[k]));
    }

    const std::vector<double> muted{0.0, 0.0};
    REQUIRE_FALSE(spatialAverage(positions, muted).has_value());
}

TEST_CASE("mismatched grids are refused", "[spatial][validation]") {
    constexpr std::size_t kBins = 4;
    const auto base = fixture(-3.0, 0.0, 1.0, kBins);

    {
        TransferSnapshot mismatched = base;
        mismatched.binWidthHz *= 2.0;
        REQUIRE_THROWS_AS(spatialAverage(std::vector<TransferSnapshot>{base, mismatched},
                                          std::vector<double>{1.0, 1.0}),
                           std::invalid_argument);
    }
    {
        TransferSnapshot mismatched = base;
        mismatched.sampleRate *= 2.0;
        REQUIRE_THROWS_AS(spatialAverage(std::vector<TransferSnapshot>{base, mismatched},
                                          std::vector<double>{1.0, 1.0}),
                           std::invalid_argument);
    }
    {
        const auto mismatched = fixture(-3.0, 0.0, 1.0, kBins + 1);
        REQUIRE_THROWS_AS(spatialAverage(std::vector<TransferSnapshot>{base, mismatched},
                                          std::vector<double>{1.0, 1.0}),
                           std::invalid_argument);
    }
    REQUIRE_THROWS_AS(
        spatialAverage(std::vector<TransferSnapshot>{base, base}, std::vector<double>{1.0}),
        std::invalid_argument);
    REQUIRE_THROWS_AS(
        spatialAverage(std::vector<TransferSnapshot>{base}, std::vector<double>{-1.0}),
        std::invalid_argument);
    REQUIRE_THROWS_AS(spatialAverage(std::vector<TransferSnapshot>{}, std::vector<double>{}),
                       std::invalid_argument);
}

TEST_CASE("trust and agreement are what they say", "[spatial][trust]") {
    constexpr std::size_t kBins = 1;

    SECTION("weightedCoherence is Sum(u*g2)/Sum(u)") {
        const std::vector<TransferSnapshot> positions{
            fixture(0.0, 0.0, 0.9, kBins), fixture(0.0, 0.0, 0.4, kBins),
            fixture(0.0, 0.0, 0.81, kBins)};
        const std::vector<double> u{1.0, 2.0, 0.5};
        const auto result = spatialAverage(positions, u);
        REQUIRE(result.has_value());
        REQUIRE(result->weightedCoherence[0] == Catch::Approx(0.6014285714285714).epsilon(1e-6));
    }

    SECTION("R for three unit vectors at 0, 120 and 240 degrees is 0") {
        const double third = 2.0 * std::numbers::pi / 3.0;
        const std::vector<TransferSnapshot> positions{
            fixture(0.0, 0.0, 1.0, kBins), fixture(0.0, third, 1.0, kBins),
            fixture(0.0, 2.0 * third, 1.0, kBins)};
        const std::vector<double> u{1.0, 1.0, 1.0};
        const auto result = spatialAverage(positions, u);
        REQUIRE(result.has_value());
        REQUIRE(result->phaseAgreement[0] == Catch::Approx(0.0).margin(1e-15));
    }
}
