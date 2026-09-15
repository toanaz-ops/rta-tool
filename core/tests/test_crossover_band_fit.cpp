// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TDD sequence for crossoverBandFit -- see
// docs/plans/2026-09-15-L7-align-impl-plan.md Task C, and the decision record
// docs/dsp/2026-09-06-l7-alignment-wizard.md Sec.4 / Sec.10.4-10.7.
//
// NO UNWRAP ANYWHERE. The estimator is complex-domain by decision
// (docs/dsp/2026-08-28-dual-fft.md Sec.6), so these fixtures never build a
// phase ramp in radians and never difference one.
//
// C1-C4: the estimate itself, its sign, its appliedDelaySamples
// correction, and the three refusals it returns instead of a default.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "CrossoverBandFixture.h"

#include <cmath>
#include <complex>
#include <optional>
#include <random>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::dsp;
using namespace rta_test;

TEST_CASE("crossoverBandFit recovers a pure delay, with R's floor derived from tau's own bound",
          "[crossover_band_fit]") {
    // C1. The two tolerances are ONE statement. R is evaluated at tau*, not at
    // tau0, so it is a function of the refinement error dtau -- asserting a
    // fixed R alongside a fixed tau bound would be asserting two unrelated
    // things and hoping.
    //
    // For this fixture (N = 121, delta = 1 Hz, dtau_max = grid/20 = 5e-8 s):
    //   (pi * 1 * 5e-8)^2 = 2.467401e-14, x (121^2-1)/6 = x 2440 -> 6.020459e-11
    // and the exact Dirichlet value is 6.020462e-11. The floor below is
    // 1 - 1.001 * that, the 0.1% absorbing a +-1-bin window-edge convention.
    //
    // A builder who changes binWidthHz MUST recompute this from the formula.
    // At 2 Hz bins (N = 61) the same arithmetic gives 6.119e-11, not 6.02e-11.
    std::vector<std::complex<double>> hA, hB;
    pureDelayPair(3.7e-3, kPi, hA, hB);

    const auto fit = crossoverBandFit(hA, hB, unitCoherence(kBins), unitCoherence(kBins), kBinHz,
                                      baseOptions());
    REQUIRE(fit.refusal == CrossoverRefusal::None);

    constexpr double kDtauMax = kGrid / 20.0;
    const double tauResidual = std::abs(fit.tauSeconds - 3.7e-3);
    const double floorFromDtau = 1.0 - 1.001 * (1.0 - dirichletAgreement(kWindowBins, kBinHz, kDtauMax));

    INFO("tau residual " << tauResidual << " s against dtau_max " << kDtauMax);
    CHECK(tauResidual <= kDtauMax);

    INFO("1 - agreement = " << (1.0 - fit.agreement) << ", derived floor allows "
                            << (1.0 - floorFromDtau));
    CHECK(fit.agreement >= floorFromDtau);

    // The weighted mean frequency of a flat 40..160 Hz window is 100 Hz, and a
    // residual delay dtau leaves EXACTLY 2*pi*f_bar*dtau of phase there. So the
    // intercept's tolerance is not a free constant either -- it is tau's bound
    // carried through the same fixture. The measured value is printed beside it.
    CHECK_THAT(fit.meanFrequencyHz, WithinAbs(100.0, 1e-9));
    const double interceptResidual = std::abs(wrapToPi(fit.interceptRadians - kPi));
    const double interceptBound = 2.0 * kPi * fit.meanFrequencyHz * kDtauMax;
    INFO("intercept residual " << interceptResidual << " rad, derived bound " << interceptBound);
    CHECK(interceptResidual <= interceptBound);

    WARN("C1 residuals: tau " << tauResidual << " s (bound " << kDtauMax << "), 1-R "
                              << (1.0 - fit.agreement) << " (floor allows " << (1.0 - floorFromDtau)
                              << "), intercept " << interceptResidual << " rad (bound "
                              << interceptBound << ")");
}

TEST_CASE("crossoverBandFit's tau changes SIGN with the fixture -- the flipped-convention case",
          "[crossover_band_fit]") {
    // C2. Positive tau means the LP side (B) arrives LATER than the HP side (A)
    // and the HP side is what gets delayed. A convention flip reads this
    // fixture as +3.7 ms and sends the operator to delay the wrong box.
    std::vector<std::complex<double>> hA, hB;
    pureDelayPair(-3.7e-3, kPi, hA, hB);

    const auto fit = crossoverBandFit(hA, hB, unitCoherence(kBins), unitCoherence(kBins), kBinHz,
                                      baseOptions());
    REQUIRE(fit.refusal == CrossoverRefusal::None);
    INFO("tau = " << fit.tauSeconds);
    CHECK(fit.tauSeconds < 0.0);
    CHECK(std::abs(fit.tauSeconds + 3.7e-3) <= kGrid / 20.0);
}

TEST_CASE("crossoverBandFit undoes appliedDelaySamples itself -- the correction cannot be forgotten",
          "[crossover_band_fit]") {
    // C3, ALIGN-R2. The record files this reconciliation under app/; split that
    // way, "off by exactly 48/fs without it" would not be a core test and no
    // fixture would lock it. Here it is a REQUIRED field of the options struct,
    // so the only way to get it wrong is to pass the wrong number, not to
    // forget the step.
    //
    // A capture with D extra samples of applied delay carries e^{-j2 pi f D/fs}.
    // The fixture puts 48 samples of that on H_B and the fit takes them back off.
    constexpr double kExtraSamples = 48.0;
    constexpr double kFs = 48000.0;
    std::vector<std::complex<double>> hA, hB;
    pureDelayPair(3.7e-3, kPi, hA, hB, kExtraSamples / kFs);

    auto corrected = baseOptions();
    corrected.appliedDelayDifferenceSamples = kExtraSamples;
    const auto withIt =
        crossoverBandFit(hA, hB, unitCoherence(kBins), unitCoherence(kBins), kBinHz, corrected);
    REQUIRE(withIt.refusal == CrossoverRefusal::None);
    INFO("with the correction: tau = " << withIt.tauSeconds);
    CHECK_THAT(withIt.tauSeconds, WithinAbs(3.7e-3, 1e-9));

    const auto without =
        crossoverBandFit(hA, hB, unitCoherence(kBins), unitCoherence(kBins), kBinHz, baseOptions());
    REQUIRE(without.refusal == CrossoverRefusal::None);
    const double offBy = without.tauSeconds - withIt.tauSeconds;
    INFO("without it: tau = " << without.tauSeconds << ", off by " << offBy
                              << " s (48/48000 = " << (kExtraSamples / kFs) << ")");
    CHECK_THAT(offBy, WithinAbs(kExtraSamples / kFs, 1e-9));
}

TEST_CASE("crossoverBandFit refuses with a reason rather than fitting over nothing",
          "[crossover_band_fit]") {
    // C4. Three distinct refusals, each named. None of them returns a default
    // number the caller cannot tell from a measurement.
    std::vector<std::complex<double>> hA, hB;
    pureDelayPair(3.7e-3, kPi, hA, hB);

    // (a) coherence absent entirely.
    const auto absent = crossoverBandFit(hA, hB, std::nullopt, std::nullopt, kBinHz, baseOptions());
    CHECK(absent.refusal == CrossoverRefusal::AllBinsAbsent);
    CHECK(absent.tauSeconds == 0.0);
    CHECK(absent.agreement == 0.0);

    // (b) exactly one gated bin: a fit needs two distinct frequencies to
    // separate a delay from a constant, and one bin must NEVER read R == 1.
    std::vector<float> oneBin(kBins, 0.0f);
    oneBin[100] = 1.0f;
    auto gated = baseOptions();
    gated.minimumGatedCoherence = 0.5;
    const auto tooFew =
        crossoverBandFit(hA, hB, std::optional<std::vector<float>>{ oneBin },
                         std::optional<std::vector<float>>{ oneBin }, kBinHz, gated);
    CHECK(tooFew.refusal == CrossoverRefusal::TooFewBins);
    CHECK(tooFew.agreement == 0.0);

    // (c) a window narrow enough to contain no bin at all.
    auto narrow = baseOptions();
    narrow.centreHz = 80.5;
    narrow.octavesEachSide = 1.0e-6;
    const auto empty = crossoverBandFit(hA, hB, unitCoherence(kBins), unitCoherence(kBins), kBinHz,
                                        narrow);
    CHECK(empty.refusal == CrossoverRefusal::RangeEmpty);
}

