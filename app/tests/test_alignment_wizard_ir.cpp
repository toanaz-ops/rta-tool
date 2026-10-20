// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ALIGN-UI wave B, task T8 (docs/plans/2026-10-20-align-ui-lane-plan.md F2):
// the cycle integer is the difference of two ARRIVALS, keyed by source, and
// never the difference of two `originIndex` values (those are t = 0 markers).

#include "AlignmentWizardFixture.h"

#include "rta/ir/Deconvolver.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

using rta::measure::AlignmentWizard;
using rta::measure::CrossoverFamily;
using rta::measure::ImpulseWitness;
using rta::measure::ProcessorInversion;
using rta::measure::Source;
using rta::measure::WizardStep;
using rta::platform::OutputEngine;
using rta::test::kChannels;
using rta::test::kFs;
using rta::test::pinkNoise;
using rta::test::runSequence;

namespace {

// Origins 12345 apart (the pre-roll jitter F2 is about), arrivals 40 for MAIN
// and 136 for SUB. A unit impulse sits `arrival` samples after each origin, so
// the witness is also a plausible IR, not only a pair of integers.
ImpulseWitness witness(std::size_t origin, long arrival) {
    ImpulseWitness w;
    w.deconvolution.samples.assign(origin + 2048, 0.0f);
    w.deconvolution.samples[origin + static_cast<std::size_t>(arrival)] = 1.0f;
    w.deconvolution.originIndex = origin;
    w.deconvolution.sampleRate = kFs;
    w.arrivalSamples = arrival;
    return w;
}

std::unique_ptr<AlignmentWizard> run(OutputEngine& engine, Source highPass) {
    engine.prepare(kFs, kChannels);
    auto wizard = std::make_unique<AlignmentWizard>(engine, pinkNoise(), rta::test::makeConfig());
    wizard->answerHighPassSide(highPass);
    wizard->answerTopology({ CrossoverFamily::LinkwitzRiley, 4 });
    wizard->answerInversion(ProcessorInversion::No);
    wizard->answerCrossoverSeed(std::nullopt);
    wizard->supplyImpulseResponse(Source::Main, witness(100, 40));
    wizard->supplyImpulseResponse(Source::Sub, witness(100 + 12345, 136));
    runSequence(*wizard, engine,
                rta::test::makeCapture("hp", rta::test::risingDb(), rta::test::constantPhase(0.0)),
                rta::test::makeCapture("lp", rta::test::fallingDb(), rta::test::constantPhase(0.0)));
    return wizard;
}

}  // namespace

TEST_CASE("T8: the cycle integer is arrival(low side) - arrival(high side), by source",
          "[alignment_wizard]") {
    SECTION("high-pass = MAIN: sub (136) is the low side, +96") {
        OutputEngine engine;
        const auto wizard = run(engine, Source::Main);
        CHECK(wizard->step() != WizardStep::AskingCycle);
        REQUIRE(wizard->cycleOffsetSamples().has_value());
        CHECK(*wizard->cycleOffsetSamples() == 96);
    }
    SECTION("high-pass = SUB: main (40) is the low side, -96") {
        OutputEngine engine;
        const auto wizard = run(engine, Source::Sub);
        CHECK(wizard->step() != WizardStep::AskingCycle);
        REQUIRE(wizard->cycleOffsetSamples().has_value());
        CHECK(*wizard->cycleOffsetSamples() == -96);
    }
}

TEST_CASE("T8: one witness alone does not answer the integer", "[alignment_wizard]") {
    OutputEngine engine;
    engine.prepare(kFs, kChannels);
    AlignmentWizard wizard(engine, pinkNoise(), rta::test::makeConfig());
    wizard.answerHighPassSide(Source::Main);
    wizard.answerTopology({ CrossoverFamily::LinkwitzRiley, 4 });
    wizard.answerInversion(ProcessorInversion::No);
    wizard.answerCrossoverSeed(std::nullopt);
    wizard.supplyImpulseResponse(Source::Sub, witness(100, 136));
    runSequence(wizard, engine,
                rta::test::makeCapture("hp", rta::test::risingDb(), rta::test::constantPhase(0.0)),
                rta::test::makeCapture("lp", rta::test::fallingDb(), rta::test::constantPhase(0.0)));
    CHECK(wizard.step() == WizardStep::AskingCycle);
    CHECK_FALSE(wizard.cycleOffsetSamples().has_value());
}
