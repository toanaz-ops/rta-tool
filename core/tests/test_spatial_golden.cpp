// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Pinned against scipy via tools/gen_spatial.py. Unlike test_transfer_golden
// and test_mtw_golden, no real engine feeds this golden -- three
// TransferSnapshots are built BY HAND from the golden's own h_*/g2 rows (a
// test file may assign `.coherence` directly -- check_coherence_gate.cmake's
// own header note), so nothing float32 is in this path but the combine's own
// result storage. R2 (docs/plans/2026-09-06-L6b-impl-plan.md): a relative
// 1e-6 -- about 8x float32 epsilon -- rather than the two-term FFT-precision
// form, since no RealFft sits between this golden and the assertions below.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "rta/dsp/SpatialAverage.h"
#include "support/Golden.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <string>
#include <vector>

using namespace rta::dsp;

namespace {

rta::test::GoldenCase spatialGolden() {
    static const auto cases = rta::test::loadGolden(std::string(RTA_GOLDEN_DIR) + "/spatial.txt");
    return cases.front();
}

double peakAbs(const std::vector<double>& values) {
    double peak = 0.0;
    for (const double v : values) peak = std::max(peak, std::abs(v));
    return peak;
}

/// Relative 1e-6 (R2: nothing float32 is in this path but the result's own
/// float storage) PLUS a floor scaled to the array's own peak -- not because
/// of RealFft's rounding here, but because a bare relative tolerance is
/// undefined right where phase or dB level legitimately cross zero.
double tolerance(double expected, double peak) { return std::abs(expected) * 1e-6 + peak * 1e-6; }

/// A TransferSnapshot with no engine behind it: h and coherence are read
/// straight off the golden's own rows for position `i`, everything else
/// filled from the golden's shared grid.
TransferSnapshot fromGolden(const rta::test::GoldenCase& golden, int i) {
    const auto& hReal = golden.row("h_real_" + std::to_string(i));
    const auto& hImag = golden.row("h_imag_" + std::to_string(i));
    const auto& g2 = golden.row("g2_" + std::to_string(i));

    TransferSnapshot snap;
    snap.sampleRate = golden.row("sample_rate").front();
    snap.binWidthHz = golden.row("bin_width_hz").front();
    snap.h.resize(hReal.size());
    snap.coherence = std::vector<float>(hReal.size());
    for (std::size_t k = 0; k < hReal.size(); ++k) {
        snap.h[k] = std::complex<double>(hReal[k], hImag[k]);
        (*snap.coherence)[k] = static_cast<float>(g2[k]);
    }
    return snap;
}

}  // namespace

TEST_CASE("the spatial golden from scipy matches L, phase, R and the weighted trust",
          "[spatial][golden]") {
    const auto golden = spatialGolden();
    const std::vector<TransferSnapshot> positions{fromGolden(golden, 0), fromGolden(golden, 1),
                                                    fromGolden(golden, 2)};
    const auto& uRow = golden.row("u");
    const std::vector<double> u(uRow.begin(), uRow.end());

    const auto result = spatialAverage(positions, u, SpatialMode::Db);
    REQUIRE(result.has_value());

    const auto& expectedL = golden.row("l_db");
    const auto& expectedPhase = golden.row("phase");
    const auto& expectedR = golden.row("r");
    const auto& expectedWeightedCoherence = golden.row("weighted_coherence");
    REQUIRE(result->magnitudeDb.size() == expectedL.size());

    const double lPeak = peakAbs(expectedL);
    const double phasePeak = peakAbs(expectedPhase);
    const double rPeak = peakAbs(expectedR);
    const double wcPeak = peakAbs(expectedWeightedCoherence);

    int checked = 0;
    for (std::size_t k = 0; k < expectedL.size(); ++k) {
        CAPTURE(k);
        ++checked;
        CHECK(result->magnitudeDb[k] ==
              Catch::Approx(expectedL[k]).margin(tolerance(expectedL[k], lPeak)));
        CHECK(result->phaseRadians[k] ==
              Catch::Approx(expectedPhase[k]).margin(tolerance(expectedPhase[k], phasePeak)));
        CHECK(result->phaseAgreement[k] ==
              Catch::Approx(expectedR[k]).margin(tolerance(expectedR[k], rPeak)));
        CHECK(result->weightedCoherence[k] ==
              Catch::Approx(expectedWeightedCoherence[k]).margin(tolerance(expectedWeightedCoherence[k], wcPeak)));
    }
    REQUIRE(checked == 513);  // an empty case list must fail, not pass vacuously
}
