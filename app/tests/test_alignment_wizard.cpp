// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-ALIGN task H (docs/plans/2026-09-15-L7-align-impl-plan.md; decision
// record docs/dsp/2026-09-06-l7-alignment-wizard.md Sec.2, Sec.9). The L7-OUT
// solo sequence and the named refusals, driven against a REAL
// rta::platform::OutputEngine with no audio hardware -- the shape
// test_delay_locator.cpp and test_eq_verify.cpp already prove the output path
// with. The fit, the polarity table and the structural scans are in
// test_alignment_wizard_verdict.cpp.

#include "AlignmentWizardFixture.h"

#include "rta/gen/Noise.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using rta::measure::AlignmentWizard;
using rta::measure::CrossoverFamily;
using rta::measure::ProcessorInversion;
using rta::measure::Source;
using rta::measure::WizardRefusal;
using rta::measure::WizardStep;
using rta::platform::OutputEngine;
using rta::platform::OutputRole;
using rta::test::kChannels;
using rta::test::kFs;
using rta::test::pinkNoise;

namespace {


void answerEverything(AlignmentWizard& wizard, Source highPassSide = Source::Main) {
    wizard.answerHighPassSide(highPassSide);
    wizard.answerTopology({ CrossoverFamily::LinkwitzRiley, 4 });
    wizard.answerInversion(ProcessorInversion::No);
    wizard.answerCrossoverSeed(std::nullopt);
}

}  // namespace

TEST_CASE("H1: feeding captures in never moves an asked answer", "[alignment_wizard]") {
    OutputEngine engine;
    engine.prepare(kFs, kChannels);
    AlignmentWizard wizard(engine, pinkNoise(), rta::test::makeConfig());

    answerEverything(wizard);
    wizard.answerTopology({ CrossoverFamily::Butterworth, 3 });
    wizard.answerInversion(ProcessorInversion::Yes);

    const auto topologyBefore = wizard.topology();
    const auto inversionBefore = wizard.inversionAnswer();
    const auto sideBefore = wizard.highPassSide();

    wizard.arm();
    REQUIRE(wizard.lastRefusal() == WizardRefusal::None);
    rta::test::pump(engine, 2);
    wizard.poll();
    wizard.submitCapture(rta::test::makeCapture("a", rta::test::risingDb(),
                                                rta::test::constantPhase(0.4)));
    rta::test::pump(engine, 2);
    wizard.poll();
    wizard.submitCapture(rta::test::makeCapture("b", rta::test::fallingDb(),
                                                rta::test::constantPhase(-1.1)));

    // BITWISE unchanged. A measurement that moved one of these would be the
    // forbidden move: 180 degrees of wiring and 180 degrees of topology are the
    // same 180 degrees to a microphone.
    CHECK(wizard.topology()->family == topologyBefore->family);
    CHECK(wizard.topology()->order == topologyBefore->order);
    CHECK(wizard.inversionAnswer() == inversionBefore);
    CHECK(wizard.highPassSide() == sideBefore);
}

TEST_CASE("H1b: arming with a question outstanding is refused, not defaulted",
          "[alignment_wizard]") {
    OutputEngine engine;
    engine.prepare(kFs, kChannels);
    AlignmentWizard wizard(engine, pinkNoise(), rta::test::makeConfig());

    wizard.arm();
    CHECK(wizard.lastRefusal() == WizardRefusal::MissingAnswer);
    CHECK(wizard.step() == WizardStep::Answering);
    CHECK(rta::test::routedCount(engine) == 0);  // nothing was touched

    // Question (d) may be answered "no seed" -- but it must be ASKED. Three of
    // four answered still refuses.
    wizard.answerHighPassSide(Source::Main);
    wizard.answerTopology({ CrossoverFamily::LinkwitzRiley, 4 });
    wizard.answerInversion(ProcessorInversion::No);
    wizard.arm();
    CHECK(wizard.lastRefusal() == WizardRefusal::MissingAnswer);

    wizard.answerCrossoverSeed(std::nullopt);
    wizard.arm();
    CHECK(wizard.lastRefusal() == WizardRefusal::None);
    CHECK(wizard.step() == WizardStep::MeasuringHigh);
}

TEST_CASE("H2: the L7-OUT sequence, in order, with strict solo at every step",
          "[alignment_wizard]") {
    OutputEngine engine;
    engine.prepare(kFs, kChannels);
    auto config = rta::test::makeConfig();
    AlignmentWizard wizard(engine, pinkNoise(), config);
    answerEverything(wizard, Source::Main);

    wizard.arm();
    REQUIRE(wizard.step() == WizardStep::MeasuringHigh);
    // STRICT solo (ALIGN-R6, owner decision 4: a SEQUENCE is strict). Exactly
    // one output routed, and it is the high-pass side's.
    CHECK(rta::test::routedCount(engine) == 1);
    CHECK(engine.role(config.mainOutputChannel) == OutputRole::Routed);
    CHECK(engine.role(config.subOutputChannel) == OutputRole::None);

    rta::test::pump(engine, 2);
    wizard.poll();
    wizard.submitCapture(rta::test::makeCapture("hp", rta::test::risingDb(),
                                                rta::test::constantPhase(0.0)));
    REQUIRE(wizard.step() == WizardStep::MeasuringLow);
    CHECK(rta::test::routedCount(engine) == 1);
    CHECK(engine.role(config.subOutputChannel) == OutputRole::Routed);
    CHECK(engine.role(config.mainOutputChannel) == OutputRole::None);

    rta::test::pump(engine, 2);
    wizard.poll();
    wizard.submitCapture(rta::test::makeCapture("lp", rta::test::fallingDb(),
                                                rta::test::constantPhase(0.0)));
    REQUIRE(wizard.verdict().has_value());

    // The last step is one signal on TWO outputs -- L7-OUT Sec.11's promise --
    // for the MEASURED sum. Exactly two, not three: the additive toggle runs
    // over a strict solo, so nothing an earlier step routed survives.
    wizard.beginMeasuredSum();
    CHECK(wizard.step() == WizardStep::MeasuringSum);
    CHECK(rta::test::routedCount(engine) == 2);
    CHECK(engine.role(config.mainOutputChannel) == OutputRole::Routed);
    CHECK(engine.role(config.subOutputChannel) == OutputRole::Routed);

    rta::test::pump(engine, 2);
    wizard.poll();
    wizard.submitMeasuredSum(rta::test::makeCapture("sum", rta::test::risingDb(),
                                                    rta::test::constantPhase(0.0)));
    CHECK(wizard.step() == WizardStep::Done);
    CHECK(wizard.measuredSum().has_value());
}

TEST_CASE("H3: the non-stationary head is waited out at EVERY step", "[alignment_wizard]") {
    OutputEngine engine;
    engine.prepare(kFs, kChannels);
    AlignmentWizard wizard(engine, pinkNoise(), rta::test::makeConfig());
    answerEverything(wizard);
    wizard.arm();

    // Output-path record Sec.11: the first 10 ms are not stationary -- 480
    // samples at 48 kHz. 256 rendered is not enough.
    rta::test::pump(engine, 1, 256);
    wizard.poll();
    CHECK_FALSE(wizard.captureWindowOpen());
    wizard.submitCapture(rta::test::makeCapture("early", rta::test::risingDb(),
                                                rta::test::constantPhase(0.0)));
    CHECK(wizard.lastRefusal() == WizardRefusal::WindowNotOpen);
    CHECK(wizard.step() == WizardStep::MeasuringHigh);

    rta::test::pump(engine, 1, 256);
    wizard.poll();
    CHECK(wizard.captureWindowOpen());
    wizard.submitCapture(rta::test::makeCapture("hp", rta::test::risingDb(),
                                                rta::test::constantPhase(0.0)));
    REQUIRE(wizard.step() == WizardStep::MeasuringLow);

    // And AGAIN for the second solo: that step re-ramped a different output
    // gate, so its own first 10 ms are not stationary either. The counter is
    // taken from the moment of the solo, not from the arm.
    CHECK_FALSE(wizard.captureWindowOpen());
    wizard.poll();
    CHECK_FALSE(wizard.captureWindowOpen());
    rta::test::pump(engine, 2);
    wizard.poll();
    CHECK(wizard.captureWindowOpen());
}

TEST_CASE("H4: a pair that does not share one engine configuration is refused by name",
          "[alignment_wizard]") {
    struct Case {
        const char* label;
        WizardRefusal expected;
        double sampleRate;
        int fftSize;
        const char* roles;
    };
    const Case cases[]{
        { "sample rate", WizardRefusal::SampleRateMismatch, 44100.0, rta::test::kFftSize,
          "ref:1 meas:2" },
        { "fft size", WizardRefusal::FftSizeMismatch, kFs, 256, "ref:1 meas:2" },
        // ALIGN-R7: CaptureMeta has no reference-channel field, so the proxy
        // for "same reference" is the channelRoles string. Honest and weak;
        // making it structural is a schema bump this lane does not do.
        { "reference", WizardRefusal::ReferenceMismatch, kFs, rta::test::kFftSize,
          "ref:3 meas:4" },
    };

    for (const auto& testCase : cases) {
        INFO(testCase.label);
        OutputEngine engine;
        engine.prepare(kFs, kChannels);
        AlignmentWizard wizard(engine, pinkNoise(), rta::test::makeConfig());
        answerEverything(wizard);
        wizard.arm();
        rta::test::pump(engine, 2);
        wizard.poll();
        wizard.submitCapture(rta::test::makeCapture("hp", rta::test::risingDb(),
                                                    rta::test::constantPhase(0.0)));
        rta::test::pump(engine, 2);
        wizard.poll();

        const std::size_t points = rta::trace::pointCountFor(testCase.fftSize);
        std::vector<float> magnitudeDb(points, -3.0f);
        std::vector<float> phase(points, 0.0f);
        wizard.submitCapture(rta::test::makeCapture("lp", magnitudeDb, phase, 0,
                                                    testCase.sampleRate, testCase.fftSize,
                                                    testCase.roles));
        CHECK(wizard.lastRefusal() == testCase.expected);
        CHECK_FALSE(wizard.verdict().has_value());  // no fit ran
    }
}

TEST_CASE("H6: the cycle integer comes from the IR pair or from the operator, never a default",
          "[alignment_wizard]") {
    OutputEngine engine;
    engine.prepare(kFs, kChannels);
    AlignmentWizard wizard(engine, pinkNoise(), rta::test::makeConfig());
    answerEverything(wizard);
    wizard.arm();
    rta::test::pump(engine, 2);
    wizard.poll();
    wizard.submitCapture(rta::test::makeCapture("hp", rta::test::risingDb(),
                                                rta::test::constantPhase(0.0)));
    rta::test::pump(engine, 2);
    wizard.poll();
    wizard.submitCapture(rta::test::makeCapture("lp", rta::test::fallingDb(),
                                                rta::test::constantPhase(0.0)));

    // No IR pair, no operator answer: the state SAYS it is asking, and the
    // integer stays absent rather than becoming a plausible zero.
    CHECK(wizard.step() == WizardStep::AskingCycle);
    CHECK_FALSE(wizard.cycleOffsetSamples().has_value());

    wizard.answerCycleOffsetSamples(7);
    CHECK(wizard.cycleOffsetSamples() == 7);
    CHECK(wizard.step() == WizardStep::Computed);
}
