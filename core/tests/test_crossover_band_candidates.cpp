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
// C5-C7: what the fit reports about its own uncertainty -- the ranked
// competing delays, the weight that is the summation cross-term, and the
// bound on the agreement figure.

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

TEST_CASE("crossoverBandFit returns the competing delay candidates instead of hiding them",
          "[crossover_band_fit]") {
    // C5. The band fit cannot resolve a delay better than the band's own width
    // allows, and the honest response is to return the competitors ranked with
    // their own R rather than to present the winner alone.
    //
    // Their agreement values are PRINTED, NOT ASSERTED: a fixture cannot know
    // which sidelobe is highest without asserting the implementation's own
    // output, which is a regression lock, not a correctness test.
    //
    // MEASURED DEVIATION from record Sec.4 / plan C5, reported not absorbed.
    // Both say the competitors sit at tau* +- n/f_bar (10 ms here). They do
    // not, and cannot: |S(tau)| = |sum_k w_k R_k e^{-j2 pi f_k tau}| depends
    // only on the SPREAD of f_k about its weighted mean -- the mean itself
    // factors out as a pure rotation that |.| discards. So the competitor
    // spacing is set by the band's WIDTH, not by its mean frequency. For a flat
    // N-bin window the envelope is the Dirichlet kernel, whose local maxima sit
    // near (m + 1/2)/(N*delta) = 12.4 ms, 20.7 ms ... from the peak. The
    // 1/f_bar spacing the record names is the ambiguity of a TIME-DOMAIN
    // narrowband correlation, which is a different estimator.
    std::vector<std::complex<double>> hA, hB;
    pureDelayPair(9.0e-3, kPi, hA, hB);

    const auto fit = crossoverBandFit(hA, hB, unitCoherence(kBins), unitCoherence(kBins), kBinHz,
                                      baseOptions());
    REQUIRE(fit.refusal == CrossoverRefusal::None);
    CHECK_THAT(fit.tauSeconds, WithinAbs(9.0e-3, kGrid / 20.0));
    REQUIRE_FALSE(fit.cycleCandidates.empty());

    for (std::size_t i = 0; i < fit.cycleCandidates.size(); ++i) {
        WARN("C5 candidate " << i << ": tau = " << fit.cycleCandidates[i].tauSeconds
                             << " s, offset from tau* = "
                             << (fit.cycleCandidates[i].tauSeconds - fit.tauSeconds)
                             << " s, R = " << fit.cycleCandidates[i].agreement);
    }

    // The best candidate IS the fit -- the list is ranked, not a leftovers bin.
    CHECK_THAT(fit.cycleCandidates.front().tauSeconds, WithinAbs(fit.tauSeconds, 1e-12));

    // The ceiling, on the winner AND on every competitor. R <= 1 by the
    // triangle inequality over a weighted sum of unit phasors (see C1). This
    // fixture's winner sits at R = 1 exactly, so the bound is tight here and a
    // mis-scaled denominator has nowhere to hide.
    CHECK(fit.agreement <= 1.0);
    for (const auto& candidate : fit.cycleCandidates) {
        INFO("candidate R = " << candidate.agreement);
        CHECK(candidate.agreement <= 1.0);
        CHECK(candidate.agreement >= 0.0);
    }

    // Every other candidate sits near a Dirichlet local maximum,
    // (m + 1/2)/(N*delta) from the peak, m >= 1. Asserted to a fifth of a lobe:
    // the true sidelobe of |sin(Nx)/(N sin x)| sits at about 1.43/(N*delta),
    // not exactly 1.5, and the difference is a property of the kernel.
    const double lobe = 1.0 / (static_cast<double>(kWindowBins) * kBinHz);
    for (std::size_t i = 1; i < fit.cycleCandidates.size(); ++i) {
        const double offset = std::abs(fit.cycleCandidates[i].tauSeconds - fit.tauSeconds);
        const double m = std::round(offset / lobe - 0.5);
        INFO("candidate " << i << " offset " << offset << " s, nearest Dirichlet lobe "
                          << ((m + 0.5) * lobe) << " s");
        CHECK(m >= 1.0);
        CHECK(std::abs(offset - (m + 0.5) * lobe) <= 0.2 * lobe);
    }
}

TEST_CASE("crossoverBandFit's competitor spacing follows the band's WIDTH, not its mean frequency",
          "[crossover_band_fit]") {
    // C5b. The decisive experiment for the deviation C5 reports, because the
    // two candidate explanations are numerically close on C5's own fixture:
    // 1/(N*delta) = 8.26 ms and 1/f_bar = 10.0 ms.
    //
    // So move the window's CENTRE and hold its WIDTH. 40-160 Hz and 240-360 Hz
    // are both 121 bins of 1 Hz, so 1/(N*delta) is 8.26 ms for both, while
    // f_bar goes 100 -> 300 Hz and 1/f_bar goes 10.0 -> 3.33 ms, a factor of 3.
    // The first competitor does not move. The spacing is the band's width.
    constexpr std::size_t kWide = 401;
    std::vector<std::complex<double>> hA(kWide, std::complex<double>{ 1.0, 0.0 });
    std::vector<std::complex<double>> hB(kWide);
    for (std::size_t k = 0; k < kWide; ++k) {
        hB[k] = std::polar(1.0, kPi - 2.0 * kPi * static_cast<double>(k) * kBinHz * 9.0e-3);
    }

    auto lowBand = baseOptions();
    lowBand.centreHz = std::sqrt(40.0 * 160.0);  // 80
    lowBand.octavesEachSide = std::log2(160.0 / 40.0) / 2.0;
    auto highBand = baseOptions();
    highBand.centreHz = std::sqrt(240.0 * 360.0);
    highBand.octavesEachSide = std::log2(360.0 / 240.0) / 2.0;

    const auto low = crossoverBandFit(hA, hB, unitCoherence(kWide), unitCoherence(kWide), kBinHz,
                                      lowBand);
    const auto high = crossoverBandFit(hA, hB, unitCoherence(kWide), unitCoherence(kWide), kBinHz,
                                       highBand);
    REQUIRE(low.refusal == CrossoverRefusal::None);
    REQUIRE(high.refusal == CrossoverRefusal::None);
    REQUIRE(low.cycleCandidates.size() >= 2);
    REQUIRE(high.cycleCandidates.size() >= 2);
    CHECK_THAT(low.meanFrequencyHz, WithinAbs(100.0, 1e-9));
    CHECK_THAT(high.meanFrequencyHz, WithinAbs(300.0, 1e-9));

    const double lowOffset = std::abs(low.cycleCandidates[1].tauSeconds - low.tauSeconds);
    const double highOffset = std::abs(high.cycleCandidates[1].tauSeconds - high.tauSeconds);
    WARN("C5b first competitor offset: 40-160 Hz band " << lowOffset << " s, 240-360 Hz band "
                                                        << highOffset << " s; 1/f_bar would be "
                                                        << (1.0 / low.meanFrequencyHz) << " vs "
                                                        << (1.0 / high.meanFrequencyHz));
    // Same to better than a tenth of a lobe -- the mean frequency does not enter.
    const double lobe = 1.0 / (static_cast<double>(kWindowBins) * kBinHz);
    CHECK(std::abs(lowOffset - highOffset) <= 0.1 * lobe);
    // And it is nowhere near 1/f_bar for the high band.
    CHECK(std::abs(highOffset - 1.0 / high.meanFrequencyHz) > 0.5 * lobe);
}

TEST_CASE("crossoverBandFit's weight IS the summation cross-term, and it is visible",
          "[crossover_band_fit]") {
    // C6. |H_A + H_B|^2 = |H_A|^2 + |H_B|^2 + 2|H_A||H_B| cos(phi_A - phi_B):
    // the coefficient of the cosine -- the only term the relative phase can
    // move -- is |H_A||H_B|. So that product IS the weight, and no constant is
    // read off any grid to produce it.
    //
    // Fixture: |H_A| = 1 everywhere; |H_B| = 1 on the lower half of the window
    // and eps = 1e-3 on the upper half, the two halves carrying relative phases
    // phi_s and phi_s + d with d = 2.0 rad exactly, equal bin counts. R_k is
    // unit, so the weighted circular mean is
    //     arg( (N/2) e^{j phi_s} + (N/2) eps e^{j(phi_s + d)} )
    // i.e. the intercept sits atan2(eps sin d, 1 + eps cos d) from phi_s -- and
    // with the weights replaced by 1 the same algebra gives
    // atan2(sin d, 1 + cos d) = d/2 = 1.0 rad. The two differ by 0.999 rad, so
    // the weighting is load-bearing and visible rather than a detail.
    //
    // DEVIATION from plan C6, measured and reported. The plan's closed form is
    // the value at tau = 0, but a free tau search does NOT stop at 0 on this
    // fixture: the two halves disagree in phase, so |S(tau)| has a non-zero
    // derivative at 0 and the fit slides to tau* ~ 3e-5 s, moving the intercept
    // by ~1e-5 rad -- four orders above the plan's 1e-9. The case therefore
    // pins tau at 0 (tauRangeSeconds = 0, a caller who has already removed the
    // delay), which is what isolates the WEIGHT, which is what C6 is for. The
    // free-tau result is printed beside it so the size of the effect is on the
    // record rather than in a derivation nobody re-ran.
    constexpr double kEps = 1e-3;
    constexpr double kD = 2.0;
    constexpr double kPhiS = 0.4;

    std::vector<std::complex<double>> hA(kBins, std::complex<double>{ 1.0, 0.0 });
    std::vector<std::complex<double>> hB(kBins, std::complex<double>{ 1.0, 0.0 });
    // Window 40..160; halves 40..99 and 101..160 (60 bins each), bin 100 left
    // out so the counts are equal exactly.
    std::vector<float> coherence(kBins, 0.0f);
    for (std::size_t k = 40; k <= 160; ++k) {
        if (k == 100) continue;
        coherence[k] = 1.0f;
        const bool upper = k > 100;
        const double magnitude = upper ? kEps : 1.0;
        const double phase = upper ? -(kPhiS + kD) : -kPhiS;  // arg H_A - arg H_B = +phi
        hB[k] = std::polar(magnitude, phase);
    }
    const std::optional<std::vector<float>> coh{ coherence };

    auto pinned = baseOptions();
    pinned.tauRangeSeconds = 0.0;
    pinned.minimumGatedCoherence = 0.5;
    const auto fit = crossoverBandFit(hA, hB, coh, coh, kBinHz, pinned);
    REQUIRE(fit.refusal == CrossoverRefusal::None);

    const double expectedShift = std::atan2(kEps * std::sin(kD), 1.0 + kEps * std::cos(kD));
    // The plan quotes this number to seven figures; check the quote itself so a
    // reader is not asked to trust a constant that was never evaluated.
    CHECK_THAT(expectedShift, WithinAbs(9.096757e-4, 1e-9));
    INFO("intercept " << fit.interceptRadians << ", expected " << (kPhiS + expectedShift));
    CHECK(std::abs(wrapToPi(fit.interceptRadians - (kPhiS + expectedShift))) <= 1e-9);

    // The unweighted variant, stated so the difference is visible: if w_k drops
    // |H_A||H_B| the same algebra gives phi_s + d/2 = phi_s + 1.0.
    const double unweighted = std::atan2(std::sin(kD), 1.0 + std::cos(kD));
    CHECK_THAT(unweighted, WithinAbs(kD * 0.5, 1e-12));
    CHECK(std::abs(unweighted - expectedShift) > 0.99);

    const auto freeTau = crossoverBandFit(hA, hB, coh, coh, kBinHz, [] {
        auto o = baseOptions();
        o.minimumGatedCoherence = 0.5;
        return o;
    }());
    WARN("C6 free-tau fit slides to tau = " << freeTau.tauSeconds << " s, intercept "
                                            << freeTau.interceptRadians << " (pinned-tau intercept "
                                            << fit.interceptRadians << ")");
}

TEST_CASE("crossoverBandFit's agreement is bounded in [0,1] by the triangle inequality",
          "[crossover_band_fit]") {
    // C7. R = |sum w_k R_k e^{-j...}| / sum w_k with every R_k on the unit
    // circle and every w_k >= 0, so |sum| <= sum by the triangle inequality.
    // This is a closed form, not a measurement -- 200 draws only demonstrate
    // that the implementation computes the thing the inequality is about.
    std::mt19937 rng{ 20260916u };
    std::uniform_real_distribution<double> mag{ 1e-3, 3.0 };
    std::uniform_real_distribution<double> phase{ -kPi, kPi };
    std::uniform_real_distribution<double> coh{ 0.0, 1.0 };

    auto options = baseOptions();
    options.tauRangeSeconds = 1.0e-4;  // 201 grid points: bounded-ness, not resolution
    options.maxCycleCandidates = 2;

    double worstLow = 1.0;
    double worstHigh = 0.0;
    for (int draw = 0; draw < 200; ++draw) {
        std::vector<std::complex<double>> hA(kBins), hB(kBins);
        std::vector<float> cohA(kBins), cohB(kBins);
        for (std::size_t k = 0; k < kBins; ++k) {
            hA[k] = std::polar(mag(rng), phase(rng));
            hB[k] = std::polar(mag(rng), phase(rng));
            cohA[k] = static_cast<float>(coh(rng));
            cohB[k] = static_cast<float>(coh(rng));
        }
        const auto fit = crossoverBandFit(hA, hB, std::optional<std::vector<float>>{ cohA },
                                          std::optional<std::vector<float>>{ cohB }, kBinHz,
                                          options);
        if (fit.refusal != CrossoverRefusal::None) continue;
        worstLow = std::min(worstLow, fit.agreement);
        worstHigh = std::max(worstHigh, fit.agreement);
        CHECK(fit.agreement >= 0.0);
        CHECK(fit.agreement <= 1.0);
        for (const auto& candidate : fit.cycleCandidates) {
            CHECK(candidate.agreement >= 0.0);
            CHECK(candidate.agreement <= 1.0);
        }
    }
    WARN("C7 agreement over 200 draws spanned [" << worstLow << ", " << worstHigh << "]");
}
