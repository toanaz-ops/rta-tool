// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ALIGN-UI lane wave A, task T1 (docs/plans/2026-10-20-align-ui-lane-plan.md):
// `Verdict::candidateTausSeconds`. The band fit returns its competing delays
// ranked by agreement because it cannot resolve a delay finer than the band's
// own width allows (core/tests/test_crossover_band_candidates.cpp, C5/C5b); the
// wizard used to keep only the winner, so the cycle integer had nothing to pick
// among. This runs the C5 fixture THROUGH the wizard -- a 40-160 Hz band, a
// pure 9 ms delay on the low-pass side -- and checks the list arrives at the
// verdict intact.
//
// Where the expectations come from (CLAUDE.md "Verification standard"): the
// winner is the closed-form delay the fixture was built with; the competitor
// sits near a Dirichlet local maximum of the window's kernel, at about
// 1.43/(N*delta) from the peak with N the window's bin count -- the property
// C5 asserts of the same fit, not a number this test copied from its output.

#include "AlignmentWizardFixture.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::measure::AlignmentWizard;
using rta::measure::CrossoverFamily;
using rta::measure::ProcessorInversion;
using rta::measure::Source;
using rta::measure::WizardStep;
using rta::platform::OutputEngine;
using rta::test::kFs;

namespace {

// 1 Hz bins: the fit window must hold more than a handful of bins at 40-160 Hz.
constexpr int kFine = 48000;
constexpr double kBinHz = kFs / static_cast<double>(kFine);
constexpr double kDelaySeconds = 9.0e-3;
constexpr double kGridSeconds = 1.0e-5;

std::size_t finePoints() { return rta::trace::pointCountFor(kFine); }

/// |H_A| rises through 0 dB at 80.5 Hz and |H_B| falls through it, 0.1 dB per
/// bin. The crossing sits BETWEEN two bins on purpose (an exact zero of
/// |H_A|-|H_B| on a bin is a tie the crossing finder owes no ruling on), and
/// the wizard's own +-1 octave window around it is 40.25-161 Hz: bins 41..161,
/// 121 of them.
std::vector<float> slopeDb(double sign) {
    std::vector<float> db(finePoints());
    for (std::size_t k = 0; k < db.size(); ++k) {
        db[k] = static_cast<float>(sign * 0.1 * (static_cast<double>(k) * kBinHz - 80.5));
    }
    return db;
}

/// arg = wrap(-2 pi f T + offset): a pure delay of T on this side.
std::vector<float> delayedPhaseFine(double delaySeconds, double offsetRadians) {
    std::vector<float> phase(finePoints());
    for (std::size_t k = 0; k < phase.size(); ++k) {
        double radians = std::remainder(
            -2.0 * rta::test::kPi * static_cast<double>(k) * kBinHz * delaySeconds + offsetRadians,
            2.0 * rta::test::kPi);
        if (radians <= -rta::test::kPi) radians += 2.0 * rta::test::kPi;
        phase[k] = static_cast<float>(radians);
    }
    return phase;
}

}  // namespace

TEST_CASE("T1: the wizard's verdict carries the fit's ranked delay candidates, winner first",
          "[alignment_wizard_candidates]") {
    OutputEngine engine;
    engine.prepare(kFs, rta::test::kChannels);
    auto config = rta::test::makeConfig();
    config.fit.tauRangeSeconds = 0.020;  // the shipped range; the competitors lie inside it
    config.fit.tauGridSeconds = kGridSeconds;
    AlignmentWizard wizard(engine, rta::test::pinkNoise(), config);
    wizard.answerHighPassSide(Source::Main);
    wizard.answerTopology({ CrossoverFamily::LinkwitzRiley, 4 });
    wizard.answerInversion(ProcessorInversion::No);
    wizard.answerCrossoverSeed(std::nullopt);

    // The first capture is the high-pass side's (main), the second the sub's.
    rta::test::runSequence(
        wizard, engine,
        rta::test::makeCapture("hp", slopeDb(+1.0), delayedPhaseFine(0.0, 0.0), 0, kFs, kFine),
        rta::test::makeCapture("lp", slopeDb(-1.0), delayedPhaseFine(kDelaySeconds, 0.0), 0, kFs, kFine));

    REQUIRE(wizard.verdict().has_value());
    const auto& verdict = *wizard.verdict();
    REQUIRE(verdict.fitRefusal == rta::dsp::CrossoverRefusal::None);
    CHECK_THAT(verdict.crossoverHz, WithinAbs(80.5, kBinHz));
    CHECK_THAT(verdict.tauSeconds, WithinAbs(kDelaySeconds, kGridSeconds));

    // The winner is the fit's own delay, and the list is ranked, not a
    // leftovers bin.
    REQUIRE(verdict.candidateTausSeconds.size() >= 2);
    CHECK_THAT(verdict.candidateTausSeconds.front(), WithinAbs(verdict.tauSeconds, 1e-12));

    // The first competitor: near a Dirichlet local maximum of the window's
    // kernel. The window holds N = 121 bins of 1 Hz (41..161 Hz inclusive), so
    // 1/(N*delta) is the lobe width and the sidelobe sits near 1.43 lobes
    // (C5: "about 1.43/(N*delta), not exactly 1.5"), asserted to a fifth of one.
    constexpr double kWindowBins = 121.0;
    const double lobe = 1.0 / (kWindowBins * kBinHz);
    const double offset = std::abs(verdict.candidateTausSeconds[1] - verdict.tauSeconds);
    INFO("first competitor offset " << offset << " s, lobe " << lobe << " s, ratio " << offset / lobe);
    WARN("T1 candidates (s): " << verdict.candidateTausSeconds[0] << ", " << verdict.candidateTausSeconds[1]
                               << (verdict.candidateTausSeconds.size() > 2
                                       ? ", " + std::to_string(verdict.candidateTausSeconds[2])
                                       : std::string()));
    CHECK(offset >= 1.3 * lobe);
    CHECK(offset <= 1.7 * lobe);
    CHECK(wizard.step() == WizardStep::AskingCycle);  // no IR pair, no answer: the integer is still owed
}
