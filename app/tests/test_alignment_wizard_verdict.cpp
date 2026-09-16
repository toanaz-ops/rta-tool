// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-ALIGN task H, the FIT half (plan H5, H9, H10): the argument contract that
// makes record Sec.3's offset and record Sec.4's fit the same quantity, the
// comparison of the fitted intercept against the ASKED topology, and what a
// collapsed R is allowed to say. The polarity table and the structural scans
// are in test_alignment_wizard_signals.cpp; the sequence and the refusals in
// test_alignment_wizard.cpp. Three files because one was 538 lines, well past
// the 400-line cap (CLAUDE.md "File length").

#include "AlignmentWizardFixture.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <optional>
#include <random>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::measure::AlignmentWizard;
using rta::measure::CrossoverFamily;
using rta::measure::ProcessorInversion;
using rta::measure::Source;
using rta::platform::OutputEngine;
using rta::test::kFs;
using rta::test::kPi;
using rta::test::pinkNoise;
using rta::test::runSequence;

TEST_CASE("H5: A is the high-pass side, the meta delay is passed once, and a transposition "
          "flips tau",
          "[alignment_wizard]") {
    constexpr double kDelaySeconds = 2.0e-4;

    // Same two captures both times. What changes is the ANSWER to question (a),
    // and with it which capture the fit receives as hA (ALIGN-R14).
    const auto delayedSide = [] {
        return rta::test::makeCapture("delayed", rta::test::risingDb(),
                                      rta::test::delayedPhase(kDelaySeconds));
    };
    const auto flatSide = [] {
        return rta::test::makeCapture("flat", rta::test::fallingDb(),
                                      rta::test::constantPhase(0.0));
    };

    OutputEngine engineA;
    engineA.prepare(kFs, rta::test::kChannels);
    AlignmentWizard wizardA(engineA, pinkNoise(), rta::test::makeConfig());
    wizardA.answerHighPassSide(Source::Main);
    wizardA.answerTopology({ CrossoverFamily::LinkwitzRiley, 4 });
    wizardA.answerInversion(ProcessorInversion::No);
    wizardA.answerCrossoverSeed(std::nullopt);
    runSequence(wizardA, engineA, delayedSide(), flatSide());
    REQUIRE(wizardA.verdict().has_value());

    OutputEngine engineB;
    engineB.prepare(kFs, rta::test::kChannels);
    AlignmentWizard wizardB(engineB, pinkNoise(), rta::test::makeConfig());
    wizardB.answerHighPassSide(Source::Sub);
    wizardB.answerTopology({ CrossoverFamily::LinkwitzRiley, 4 });
    wizardB.answerInversion(ProcessorInversion::No);
    wizardB.answerCrossoverSeed(std::nullopt);
    runSequence(wizardB, engineB, flatSide(), delayedSide());
    REQUIRE(wizardB.verdict().has_value());

    const double tauA = wizardA.verdict()->tauSeconds;
    const double tauB = wizardB.verdict()->tauSeconds;
    INFO("tau with the delayed capture as hA: " << tauA << " s; transposed: " << tauB << " s");
    // The delayed side as hA (the high-pass side) means the LOW-pass side
    // arrives EARLIER, so tau is negative -- the same sign convention
    // DelayEstimate::delaySamples carries. Transposing the answer flips it,
    // and the grid is symmetric about zero, so the two are exact negatives.
    CHECK(tauA < 0.0);
    CHECK(tauB > 0.0);
    CHECK_THAT(tauA, WithinAbs(-tauB, 1e-10));
    // One grid step. The fixture's phase crosses Trace's FLOAT storage, so a
    // tolerance shaped for a double fixture would be a float32-blind bound
    // (memory/float32-fft-precision.md); the residual is printed above.
    CHECK_THAT(std::abs(tauA), WithinAbs(kDelaySeconds, 1.0e-5));

    // R in CLOSED FORM, not as a round number. Added 2026-09-16 after PR #9's
    // verifier (defect D4): `agreement > 0.999` was a bound nothing derived.
    //
    // At the fitted tau every bin carries the SAME residual phase
    // 2*pi*f_k*dtau, so over N bins spaced Delta the circular mean loses
    //
    //     1 - R = (pi*Delta*dtau)^2 * (N^2 - 1) / 6
    //
    // -- the plan's ALIGN-R12(a), derived there for exactly this fit. The
    // window is +-1 octave around the 12 kHz crossing of risingDb()/fallingDb()
    // at a 93.75 Hz bin, so bins 64..256 take part: N = 193. `dtau` is the
    // fit's own residual, measured two lines up, not assumed.
    //
    // The 1e-12 floor is float32's: the fixture's phase is stored as a float,
    // whose worst per-bin error near pi is 2^-23*pi = 3.7e-7 rad, and a
    // circular mean of unit phasors each off by eps loses eps^2/2 -- under
    // 7e-14 (memory/float32-fft-precision.md).
    const double dTau = std::abs(std::abs(tauA) - kDelaySeconds);
    constexpr double kBinsInWindow = 193.0;
    const double derivedLoss = std::pow(kPi * rta::test::binWidthHz() * dTau, 2.0)
                               * (kBinsInWindow * kBinsInWindow - 1.0) / 6.0;
    INFO("dtau " << dTau << " s; 1 - R measured " << (1.0 - wizardA.verdict()->agreement)
                 << " against the derived " << (1.01 * derivedLoss + 1e-12));
    CHECK(1.0 - wizardA.verdict()->agreement <= 1.01 * derivedLoss + 1e-12);

    // The appliedDelaySamples reconciliation, passed ONCE and in the right
    // direction: D_B - D_A with A the high-pass side (ALIGN-R2). The wizard
    // applies no rotation of its own -- core owns it, because a required field
    // of the options struct cannot be forgotten.
    SECTION("the meta delay difference is D_B - D_A, exactly") {
        OutputEngine engine;
        engine.prepare(kFs, rta::test::kChannels);
        AlignmentWizard wizard(engine, pinkNoise(), rta::test::makeConfig());
        wizard.answerHighPassSide(Source::Main);
        wizard.answerTopology({ CrossoverFamily::LinkwitzRiley, 4 });
        wizard.answerInversion(ProcessorInversion::No);
        wizard.answerCrossoverSeed(std::nullopt);
        runSequence(wizard, engine,
                    rta::test::makeCapture("hp", rta::test::risingDb(),
                                           rta::test::constantPhase(0.0), 11),
                    rta::test::makeCapture("lp", rta::test::fallingDb(),
                                           rta::test::constantPhase(0.0), 15));
        CHECK(wizard.lastFitOptions().appliedDelayDifferenceSamples == 4.0);
        CHECK(wizard.lastFitOptions().sampleRate == kFs);
    }
}

TEST_CASE("H9: the verdict compares the fitted intercept against the ASKED topology, with a "
          "sign that can go red",
          "[alignment_wizard]") {
    // A constructed pair whose true offset is exactly +pi/2 with a KNOWN
    // high-pass side: arg(H_A) - arg(H_B) = pi/2 at every bin.
    const auto highSide = [] {
        return rta::test::makeCapture("hp", rta::test::risingDb(),
                                      rta::test::constantPhase(kPi / 2.0));
    };
    const auto lowSide = [] {
        return rta::test::makeCapture("lp", rta::test::fallingDb(),
                                      rta::test::constantPhase(0.0));
    };

    const auto deltaFor = [&](int order) {
        OutputEngine engine;
        engine.prepare(kFs, rta::test::kChannels);
        AlignmentWizard wizard(engine, pinkNoise(), rta::test::makeConfig());
        wizard.answerHighPassSide(Source::Main);
        wizard.answerTopology({ CrossoverFamily::Butterworth, order });
        wizard.answerInversion(ProcessorInversion::No);
        wizard.answerCrossoverSeed(std::nullopt);
        runSequence(wizard, engine, highSide(), lowSide());
        REQUIRE(wizard.verdict().has_value());
        return *wizard.verdict();
    };

    // RULING FROM PR #3 (merged), docs/research/2026-09-15-l7-align-order4-
    // probe.md Sec.8: the N*90-degree identity holds exactly under this repo's
    // convention, so record Sec.3's table ships unchanged and BW1 and BW3 are
    // half a turn apart. A transposed table, or a negated expectedOffset,
    // moves both of these by pi.
    //
    // The plan asks for 1e-9. The fixture's phase is stored as a FLOAT on the
    // way in -- `float(pi/2)` is 4.37e-8 above pi/2 -- so 1e-9 here would be a
    // float32-blind tolerance, the shape memory/float32-fft-precision.md
    // warns about. The bound is 1e-6 and the intercept's residual against the
    // exact pi/2 is printed beside it; what the case is FOR is the half-turn
    // between BW1 and BW3, which no float can blur.
    const auto bw1 = deltaFor(1);
    INFO("BW1 intercept residual against pi/2: " << std::abs(bw1.interceptRadians - kPi / 2.0)
                                                 << " (float storage costs ~4.4e-8)");
    CHECK_THAT(bw1.interceptRadians, WithinAbs(kPi / 2.0, 1e-6));
    CHECK_THAT(bw1.expectedRadians, WithinAbs(kPi / 2.0, 1e-15));
    CHECK_THAT(bw1.deltaRadians, WithinAbs(0.0, 1e-6));

    const auto bw3 = deltaFor(3);
    CHECK_THAT(bw3.expectedRadians, WithinAbs(-kPi / 2.0, 1e-15));
    CHECK_THAT(std::abs(bw3.deltaRadians), WithinAbs(kPi, 1e-6));

    // Question (c)'s Unknown branch is TWO lines, and nothing picks between
    // them (record Sec.13.3).
    OutputEngine engine;
    engine.prepare(kFs, rta::test::kChannels);
    AlignmentWizard wizard(engine, pinkNoise(), rta::test::makeConfig());
    wizard.answerHighPassSide(Source::Main);
    wizard.answerTopology({ CrossoverFamily::Butterworth, 1 });
    wizard.answerInversion(ProcessorInversion::Unknown);
    wizard.answerCrossoverSeed(std::nullopt);
    runSequence(wizard, engine, highSide(), lowSide());
    REQUIRE(wizard.verdict().has_value());
    CHECK(wizard.verdict()->expectedAmbiguous);
    CHECK_THAT(std::abs(wizard.verdict()->alternativeDeltaRadians), WithinAbs(kPi, 1e-6));
}

TEST_CASE("H10: a low R says 'not a matched pair' and offers no topology change",
          "[alignment_wizard]") {
    // Phase noise per bin: there is no single delay plus single constant that
    // explains this band, and R is what says so.
    std::mt19937 rng{ 20260916u };
    std::uniform_real_distribution<double> uniform{ -kPi, kPi };
    std::vector<float> scrambled(rta::test::pointCount());
    for (auto& value : scrambled) value = static_cast<float>(uniform(rng));

    OutputEngine engine;
    engine.prepare(kFs, rta::test::kChannels);
    AlignmentWizard wizard(engine, pinkNoise(), rta::test::makeConfig());
    wizard.answerHighPassSide(Source::Main);
    const rta::measure::Topology asked{ CrossoverFamily::LinkwitzRiley, 4 };
    wizard.answerTopology(asked);
    wizard.answerInversion(ProcessorInversion::No);
    wizard.answerCrossoverSeed(std::nullopt);
    runSequence(wizard, engine,
                rta::test::makeCapture("hp", rta::test::risingDb(), scrambled),
                rta::test::makeCapture("lp", rta::test::fallingDb(),
                                       rta::test::constantPhase(0.0)));

    REQUIRE(wizard.verdict().has_value());
    // WHY R collapses, written down (PR #9 verifier, defect D4). With a uniform
    // random phase per bin, R = |sum w_k e^{j theta_k}| / sum w_k is the length
    // of a weighted random walk on the unit circle: Rayleigh-distributed,
    // concentrating at 1/sqrt(N_eff) with N_eff = (sum w)^2 / sum w^2. So R
    // tends to 0 as the band widens, and "R collapses on a pair that is not one
    // delay plus one constant" is a property of the BAND, not of this seed.
    //
    // Measured here: 0.16141, which implies N_eff ~ 38 -- consistent with the
    // fit's weight |H_A||H_B| concentrating near the crossover rather than
    // spreading over all 193 window bins. Even at that small N_eff the Rayleigh
    // tail gives P(R > 0.5) = exp(-N_eff * 0.25) = e^-9.6 ~ 7e-5, so the bar
    // below is a bound this fixture clears by construction. It is deliberately
    // the SAME 0.5 as the caller-supplied `matchedPairAgreement`, because what
    // the case asserts is that the shipped verdict crosses the operator's own
    // stated bar -- not that R takes some particular value.
    INFO("agreement " << wizard.verdict()->agreement);
    CHECK(wizard.verdict()->agreement < 0.5);
    CHECK_FALSE(wizard.verdict()->matchedPair);
    // The answer the operator gave is untouched. An even-order sign
    // discrepancy is evidence about the SYSTEM, never about a convention
    // (PR #3 Sec.8) -- and a collapsed R is evidence about the pair.
    CHECK(wizard.topology()->family == asked.family);
    CHECK(wizard.topology()->order == asked.order);
}
