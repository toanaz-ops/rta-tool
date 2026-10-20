// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ALIGN-UI wave B, task T8 (docs/plans/2026-10-20-align-ui-lane-plan.md F2) and
// the PR #72 verifier-round-1 fix: the cycle integer is the difference of two
// ARRIVALS keyed by source (never of two `originIndex` values, which are t = 0
// markers), less the named topology's peak lag, and it is used only when it is
// clearly nearest one fit candidate.

#include "AlignmentWizardFixture.h"

#include "measure/CycleArrival.h"

#include "rta/ir/Deconvolver.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

using rta::measure::AlignmentWizard;
using rta::measure::CrossoverFamily;
using rta::measure::ImpulseWitness;
using rta::measure::ProcessorInversion;
using rta::measure::Source;
using rta::measure::Topology;
using rta::measure::WizardStep;
using rta::platform::OutputEngine;
using rta::test::kChannels;
using rta::test::kFs;
using rta::test::pinkNoise;
using rta::test::runSequence;

namespace {

constexpr Topology kLr4{ CrossoverFamily::LinkwitzRiley, 4 };

// A unit impulse `arrival` samples after an origin, so the witness is a
// plausible IR and not only a pair of integers.
ImpulseWitness witness(std::size_t origin, long arrival) {
    ImpulseWitness w;
    w.deconvolution.samples.assign(origin + 2048, 0.0f);
    w.deconvolution.samples[origin + static_cast<std::size_t>(arrival)] = 1.0f;
    w.deconvolution.originIndex = origin;
    w.deconvolution.sampleRate = kFs;
    w.arrivalSamples = arrival;
    return w;
}

/// Magnitudes that cross at bin 8 (750 Hz), not at the fixture's mid-band 12 kHz:
/// an LR-4 low-pass peaks 0.46/fc late, about 29 samples at 750 Hz but ~2 at 12
/// kHz, so only a crossover this low makes the peak-lag correction large enough
/// to matter against the guard (a quarter of the ~0.8 ms candidate spacing, ~10
/// samples).
std::vector<float> crossingDb(bool rising) {
    constexpr double kCrossBin = 8.0;
    std::vector<float> db(rta::test::pointCount());
    for (std::size_t k = 0; k < db.size(); ++k) {
        const double d = 2.0 * (static_cast<double>(k) - kCrossBin);
        db[k] = static_cast<float>(rising ? d : -d);
    }
    return db;
}

/// The low-pass side arrives later by `phaseDelaySeconds` in the captures, and
/// `mainArrival` / `subArrival` in the IRs; origins 12345 apart (the pre-roll
/// jitter F2 is about).
std::unique_ptr<AlignmentWizard> run(OutputEngine& engine, Source highPass, double phaseDelaySeconds,
                                     long mainArrival, long subArrival) {
    engine.prepare(kFs, kChannels);
    auto wizard = std::make_unique<AlignmentWizard>(engine, pinkNoise(), rta::test::makeConfig());
    wizard->answerHighPassSide(highPass);
    wizard->answerTopology(kLr4);
    wizard->answerInversion(ProcessorInversion::No);
    wizard->answerCrossoverSeed(std::nullopt);
    wizard->supplyImpulseResponse(Source::Main, witness(100, mainArrival));
    wizard->supplyImpulseResponse(Source::Sub, witness(100 + 12345, subArrival));
    runSequence(*wizard, engine,
                rta::test::makeCapture("hp", crossingDb(true), rta::test::constantPhase(0.0)),
                rta::test::makeCapture("lp", crossingDb(false), rta::test::delayedPhase(phaseDelaySeconds)));
    return wizard;
}

/// The topology's own peak-lag correction at the crossover this fixture fits.
long lagDifference(double crossoverHz) {
    return std::lround(rta::measure::topologyPeakLagSamples(kLr4, false, crossoverHz, kFs)
                       - rta::measure::topologyPeakLagSamples(kLr4, true, crossoverHz, kFs));
}

/// Two passes: the first reads where the fixture crosses over, the second puts
/// the captures' own delay exactly where the corrected IR integer says it is,
/// so the guard sees an integer that sits ON a candidate.
struct Fixture {
    long expected = 0;       ///< (low arrival - high arrival) - lag difference, in samples
    double crossoverHz = 0;
};
Fixture measureFixture(Source highPass, long mainArrival, long subArrival) {
    OutputEngine engine;
    const auto probe = run(engine, highPass, 0.0, mainArrival, subArrival);
    Fixture f;
    f.crossoverHz = probe->verdict()->crossoverHz;
    const long lowMinusHigh = (highPass == Source::Main) ? subArrival - mainArrival : mainArrival - subArrival;
    f.expected = lowMinusHigh - lagDifference(f.crossoverHz);
    return f;
}

}  // namespace

TEST_CASE("T8: the integer is arrival(low) - arrival(high) - lag, keyed by source", "[alignment_wizard]") {
    SECTION("high-pass = MAIN: sub is the low side") {
        const auto f = measureFixture(Source::Main, 40, 100);
        OutputEngine engine;
        const auto wizard = run(engine, Source::Main, static_cast<double>(f.expected) / kFs, 40, 100);
        REQUIRE(wizard->irCycleHintSamples().has_value());
        CHECK(*wizard->irCycleHintSamples() == f.expected);
        CHECK(wizard->step() != WizardStep::AskingCycle);
        CHECK(wizard->cycleOffsetFromIr());
        REQUIRE(wizard->cycleOffsetSamples().has_value());
        CHECK(*wizard->cycleOffsetSamples() == f.expected);
    }
    SECTION("high-pass = SUB: main is the low side (a fixed mapping reads the opposite sign)") {
        const auto f = measureFixture(Source::Sub, 100, 40);
        OutputEngine engine;
        const auto wizard = run(engine, Source::Sub, static_cast<double>(f.expected) / kFs, 100, 40);
        REQUIRE(wizard->irCycleHintSamples().has_value());
        CHECK(*wizard->irCycleHintSamples() == f.expected);
        CHECK(wizard->step() != WizardStep::AskingCycle);
        REQUIRE(wizard->cycleOffsetSamples().has_value());
        CHECK(*wizard->cycleOffsetSamples() == f.expected);
    }
    // run() puts the origins 12345 apart; an origin-difference integer would
    // read 12345 in either section, and both would fail.
}

TEST_CASE("T8: an integer that sits between candidates is a hint, not an answer", "[alignment_wizard]") {
    // Arrival difference 10 ms: well past a quarter of the candidate spacing
    // from every candidate the fit offers around 0.46 ms.
    const auto f = measureFixture(Source::Main, 40, 100);
    OutputEngine engine;
    const auto wizard = run(engine, Source::Main, static_cast<double>(f.expected) / kFs, 40, 100 + 480);
    REQUIRE(wizard->irCycleHintSamples().has_value());
    CHECK(*wizard->irCycleHintSamples() == f.expected + 480);
    CHECK(wizard->step() == WizardStep::AskingCycle);
    CHECK_FALSE(wizard->cycleOffsetSamples().has_value());
    CHECK_FALSE(wizard->cycleOffsetFromIr());
}

TEST_CASE("T8: a typed offset overrides the IR integer, before and after the fit", "[alignment_wizard]") {
    const auto f = measureFixture(Source::Main, 40, 100);
    OutputEngine engine;
    const auto wizard = run(engine, Source::Main, static_cast<double>(f.expected) / kFs, 40, 100);
    REQUIRE(wizard->cycleOffsetFromIr());
    wizard->answerCycleOffsetSamples(7);  // after the fit, in Computed
    CHECK(*wizard->cycleOffsetSamples() == 7);
    CHECK_FALSE(wizard->cycleOffsetFromIr());
    CHECK(*wizard->irCycleHintSamples() == f.expected);  // the hint is still reported
}

TEST_CASE("T8: one witness alone does not answer the integer", "[alignment_wizard]") {
    OutputEngine engine;
    engine.prepare(kFs, kChannels);
    AlignmentWizard wizard(engine, pinkNoise(), rta::test::makeConfig());
    wizard.answerHighPassSide(Source::Main);
    wizard.answerTopology(kLr4);
    wizard.answerInversion(ProcessorInversion::No);
    wizard.answerCrossoverSeed(std::nullopt);
    wizard.supplyImpulseResponse(Source::Sub, witness(100, 136));
    runSequence(wizard, engine,
                rta::test::makeCapture("hp", rta::test::risingDb(), rta::test::constantPhase(0.0)),
                rta::test::makeCapture("lp", rta::test::fallingDb(), rta::test::constantPhase(0.0)));
    CHECK(wizard.step() == WizardStep::AskingCycle);
    CHECK_FALSE(wizard.cycleOffsetSamples().has_value());
    CHECK_FALSE(wizard.irCycleHintSamples().has_value());
}
