// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-EQ UI task T3 (docs/plans/2026-09-29-eq-ui-lane-plan.md, decision D11):
// EqFirDesign realises the summed response of the filters NOT yet in the rig.
// Every expected value below is a closed form: bin k of an M-point grid is at
// k*fs/M, a peaking filter reads exactly its gain at fc, and every RBJ
// peaking filter is 0 dB at DC and at Nyquist (EQ record Sec.3).
//
// N = 4096 as the plan states it: core's design samples an even-length filter
// on the half-sample grid (FIR record Sec.4, amendment 2026-09-29). The odd
// neighbour N = 4095 shares M = 32768 and is kept below as the control that
// the closed forms hold for both parities.
#include "measure/EqFirDesign.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

using rta::dsp::FirPhase;
using rta::eq::FilterSpec;
using rta::eq::FilterType;
using rta::measure::CommittedFilter;
using rta::measure::designEqFir;
using rta::measure::eqFirGridSize;
using rta::measure::eqFirMagnitudeHalfGrid;

namespace {

constexpr double kFs = 48000.0;
constexpr std::size_t kTaps = 4096;

// M >= 8N (FIR record Sec.2): 8*4096 = 32768 = 2^15 exactly, so
// bin k is at k*48000/32768 and bin 1024 is at 1024*48000/32768 = 1500 Hz.
constexpr std::size_t kBin1500 = 1024;

CommittedFilter peak1500(double gainDb, bool applied) {
    return CommittedFilter{ FilterSpec{ FilterType::Peaking, 1500.0, 2.0, gainDb }, applied };
}

/// |H| at `hz`, summed directly from the taps: a second computation,
/// independent of the FFT designFir used.
double dftMagnitude(const std::vector<float>& taps, double hz) {
    const double w = 2.0 * std::numbers::pi * hz / kFs;
    std::complex<double> h{ 0.0, 0.0 };
    for (std::size_t n = 0; n < taps.size(); ++n) {
        h += static_cast<double>(taps[n]) * std::polar(1.0, -w * static_cast<double>(n));
    }
    return std::abs(h);
}

}  // namespace

TEST_CASE("EqFirDesign: the grid is M = smallest power of two >= 8N", "[eq_fir_design]") {
    CHECK(eqFirGridSize(1024) == 8192);
    CHECK(eqFirGridSize(4096) == 32768);
    CHECK(eqFirGridSize(6144) == 65536);   // 8*6144 = 49152 rounds UP to 2^16
    CHECK(eqFirGridSize(8192) == 65536);   // 8*8192 = 65536 = 2^16 exactly
    CHECK(eqFirGridSize(4095) == 32768);   // the odd neighbours share their grid
    CHECK(eqFirGridSize(1023) == 8192);

    const std::vector<CommittedFilter> filters{ peak1500(6.0, false) };
    const auto grid = eqFirMagnitudeHalfGrid(filters, kFs, kTaps);
    REQUIRE(grid.size() == 16385);  // M/2 + 1

    // Peaking, +6 dB, fc = 1500 Hz: fc lands exactly on bin 1024, where the
    // response is exactly its gain, 10^(6/20).
    CHECK(static_cast<double>(grid[kBin1500]) == Catch::Approx(std::pow(10.0, 6.0 / 20.0)).margin(1e-5));
    // R = 0 at both ends of the spectrum.
    CHECK(static_cast<double>(grid.front()) == Catch::Approx(1.0).margin(1e-6));
    CHECK(static_cast<double>(grid.back()) == Catch::Approx(1.0).margin(1e-6));
}

TEST_CASE("EqFirDesign: an empty (or all-applied) set is exactly 1.0f in every bin",
          "[eq_fir_design]") {
    for (const auto& filters : { std::vector<CommittedFilter>{},
                                 std::vector<CommittedFilter>{ peak1500(6.0, true) } }) {
        const auto grid = eqFirMagnitudeHalfGrid(filters, kFs, kTaps);
        REQUIRE(grid.size() == 16385);
        bool allOne = true;
        for (const float v : grid) allOne = allOne && (v == 1.0f);
        CHECK(allOne);
    }
}

TEST_CASE("EqFirDesign: only filters NOT marked applied are realised (D11)", "[eq_fir_design]") {
    // An applied +6 dB filter is already in the rig. If it were summed in as
    // well, bin 1024 would read 10^(6/20) = 1.995 instead of 1.
    const std::vector<CommittedFilter> appliedOnly{ peak1500(6.0, true) };
    const auto grid = eqFirMagnitudeHalfGrid(appliedOnly, kFs, kTaps);
    CHECK(grid[kBin1500] == 1.0f);

    // Mixed: +6 dB applied, -3 dB not. Only the -3 dB is realised -- 10^(-3/20).
    const std::vector<CommittedFilter> mixed{ peak1500(6.0, true), peak1500(-3.0, false) };
    const auto mixedGrid = eqFirMagnitudeHalfGrid(mixed, kFs, kTaps);
    CHECK(static_cast<double>(mixedGrid[kBin1500]) ==
          Catch::Approx(std::pow(10.0, -3.0 / 20.0)).margin(1e-5));
}

TEST_CASE("EqFirDesign: designEqFir reports M and the group delay, and the taps realise the target",
          "[eq_fir_design]") {
    const std::vector<CommittedFilter> filters{ peak1500(6.0, false) };

    for (const auto phase : { FirPhase::Linear, FirPhase::Minimum }) {
        CAPTURE(phase == FirPhase::Linear);
        const auto result = designEqFir(filters, kFs, kTaps, phase);
        REQUIRE(result.taps.size() == kTaps);
        CHECK(result.designFftSize == 32768);
        CHECK(result.sampleRate == kFs);
        if (phase == FirPhase::Linear) {
            // FirResult reports N/2 = 2048 (integer): the true even-N delay is
            // (N-1)/2 = 2047.5 samples, half a sample lower (FirDesign.h).
            CHECK(result.groupDelaySamples == kTaps / 2);
        } else {
            CHECK(result.groupDelaySamples == 0);
        }

        // Independent check on the taps themselves. The Hann window smooths
        // the response on the scale of its main lobe (about 47 Hz here)
        // against a 750 Hz wide peak, so the error is well under the 1%
        // allowed at the peak; DC and 10 kHz are 0 dB targets, within 1%.
        CHECK(dftMagnitude(result.taps, 1500.0) == Catch::Approx(std::pow(10.0, 6.0 / 20.0)).epsilon(0.01));
        CHECK(dftMagnitude(result.taps, 0.0) == Catch::Approx(1.0).epsilon(0.01));
        CHECK(dftMagnitude(result.taps, 10000.0) == Catch::Approx(1.0).epsilon(0.01));
    }
}

TEST_CASE("EqFirDesign: even N realises the peaking filter's closed-form gain (the lane's own measurement)",
          "[eq_fir_design]") {
    // The measurement that found the core defect: +6 dB, fc 1500 Hz, Q 2,
    // fs 48000, N 4096 read 3.02 at 1500 Hz (target 1.995) and 2.04 at DC
    // before core's even-N design was fixed. Closed form: an RBJ peaking
    // biquad reads exactly 10^(G/20) at fc and exactly 1 at DC.
    //
    // Tolerance at fc, derived: the Hann window smooths |H| by its frequency
    // kernel, whose variance is sigma^2 = fs^2/(2 N^2) (from w''(0) of a Hann
    // of length N/fs seconds), so the design reads |H(fc)| + (1/2) sigma^2
    // |H''(fc)|. |H''(fc)| = 2.15e-5 per Hz^2 for this filter (second
    // difference of the biquad's own response), sigma^2 = 68.6 Hz^2 at
    // N = 4096, giving a smoothing of -7.4e-4. 1.5e-3 is twice that. The odd
    // control 4095 is asserted against the SAME closed form and bound, so a
    // parity-specific error cannot hide inside it. DC: the peak's skirt is
    // 1500 Hz away = 128 bins, so 1e-3 is a margin, not a fit.
    const std::vector<CommittedFilter> filters{ peak1500(6.0, false) };
    const double target = std::pow(10.0, 6.0 / 20.0);

    for (const std::size_t taps : { std::size_t{ 4095 }, std::size_t{ 4096 } }) {
        for (const auto phase : { FirPhase::Linear, FirPhase::Minimum }) {
            const auto result = designEqFir(filters, kFs, taps, phase);
            REQUIRE(result.taps.size() == taps);
            const double atFc = dftMagnitude(result.taps, 1500.0);
            const double atDc = dftMagnitude(result.taps, 0.0);
            CAPTURE(taps, phase == FirPhase::Linear, atFc, atDc, target);
            CHECK(std::abs(atFc - target) <= 1.5e-3);
            CHECK(std::abs(atDc - 1.0) <= 1e-3);
        }
    }

    // A flat (empty) target is a unit-gain filter at any length: DC = 1.
    const std::vector<CommittedFilter> none;
    for (const std::size_t taps : { std::size_t{ 1024 }, std::size_t{ 4096 }, std::size_t{ 6144 } }) {
        const auto flat = designEqFir(none, kFs, taps, FirPhase::Linear);
        CAPTURE(taps);
        CHECK(dftMagnitude(flat.taps, 0.0) == Catch::Approx(1.0).epsilon(1e-3));
    }
}

TEST_CASE("EqFirDesign: the largest length and the message-thread cost are measured, not assumed",
          "[eq_fir_design]") {
    // Plan D9 keeps the FIR on the message thread. The cost is REPORTED, not
    // asserted (a wall-clock bound is a flaky gate); the PR body carries it.
    std::vector<CommittedFilter> six;
    for (int i = 0; i < 6; ++i) {
        six.push_back(CommittedFilter{ FilterSpec{ FilterType::Peaking, 200.0 * (i + 1), 2.0, 3.0 }, false });
    }
    const auto t0 = std::chrono::steady_clock::now();
    const auto result = designEqFir(six, kFs, 8192, FirPhase::Minimum);
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    WARN("designEqFir, 6 filters, 8192 taps, minimum phase: " << ms << " ms");
    CHECK(result.taps.size() == 8192);
    CHECK(result.designFftSize == 65536);  // nextPow2(8 * 8192 = 65536)
}
