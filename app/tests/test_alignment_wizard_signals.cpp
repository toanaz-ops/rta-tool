// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-ALIGN task H, the SIGNALS half (plan H7, H8, and the structural side of
// H1/H5/H6/H10): the polarity-signal table that asks instead of picking, the
// invariance that keeps a time-domain sign out of the verdict, and the scan
// that keeps the forbidden move out of the source. The fit is in
// test_alignment_wizard_verdict.cpp.

#include "AlignmentWizardFixture.h"

#include "rta/dsp/Biquad.h"
#include "rta/dsp/ButterworthDesign.h"
#include "rta/gen/Sweep.h"
#include "rta/ir/Deconvolver.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using rta::measure::AlignmentWizard;
using rta::measure::CrossoverFamily;
using rta::measure::PolaritySignalKind;
using rta::measure::ProcessorInversion;
using rta::measure::SignalStanding;
using rta::measure::Source;
using rta::measure::WizardStep;
using rta::platform::OutputEngine;
using rta::test::codeLines;
using rta::test::kFs;
using rta::test::pinkNoise;
using rta::test::runSequence;

namespace {

// --- the sweep-driven impulse responses the polarity table reads -----------

rta::gen::Sweep::Config sweepConfig() {
    rta::gen::Sweep::Config config;
    config.sampleRate = kFs;
    config.startHz = 20.0;
    config.endHz = 20000.0;
    config.durationSec = 2.0;
    return config;
}

/// The same construction test_ir_polarity.cpp measures decision 6b's figures
/// with: a real sweep through a Butterworth band-pass, deconvolved.
rta::ir::Deconvolution measureThrough(double lowHz, double highHz, int sections, float drive) {
    rta::gen::Sweep sweep(sweepConfig());
    std::vector<float> excitation(sweep.lengthSamples());
    sweep.process(excitation);

    const auto design = rta::dsp::ButterworthDesign::bandPass(lowHz, highHz, kFs, sections);
    rta::dsp::BiquadCascade cascade(design.sections);
    std::vector<float> driven(excitation.size());
    for (std::size_t i = 0; i < excitation.size(); ++i) {
        driven[i] = static_cast<float>(
            cascade.processSample(static_cast<double>(drive) * excitation[i]));
    }

    rta::ir::DeconvolverConfig config;
    config.sampleRate = kFs;
    config.harmonicSpacingL = sweep.lengthConstantL();
    config.excitationLowHz = sweepConfig().startHz;
    config.excitationHighHz = sweepConfig().endHz;
    config.trustedLowHz = sweep.validBandLowHz();
    config.trustedHighHz = sweep.validBandHighHz();
    return rta::ir::deconvolve(driven, sweep.buildInverseFilter(), config);
}

/// A two-way box whose tweeter is inverted, crossed at 2 kHz. This is
/// Polarity.h's documented limit 1 -- "a multi-way box with a section inverted
/// by design is ill-posed for any checker that has only the measurement to
/// read" -- and MEASURED here it is the one case where the two eligible signals
/// genuinely disagree: findPolarity reads the first arriving wavefront (the
/// inverted tweeter) NEGATIVE with margin 1.00, while rho over the whole
/// arrival window is dominated by the un-inverted low end and reads POSITIVE
/// at 0.7986. Both readings are true about different things, which is exactly
/// why the UI must ask instead of picking (research D7).
///
/// Measured, not assumed: at a 800 Hz and a 1200 Hz crossover the two AGREE
/// (rho goes negative with the tweeter), and at 3500 Hz findPolarity reads
/// positive again. 2000 Hz is the cell where they part.
rta::ir::Deconvolution twoWayWithInvertedTweeter(double crossoverHz) {
    auto woofer = measureThrough(60.0, crossoverHz, 4, +1.0f);
    const auto tweeter = measureThrough(crossoverHz, 15000.0, 4, -1.0f);
    for (std::size_t i = 0; i < woofer.samples.size() && i < tweeter.samples.size(); ++i) {
        woofer.samples[i] += tweeter.samples[i];
    }
    return woofer;
}

const rta::measure::PolaritySignal* signalOf(const AlignmentWizard& wizard,
                                             PolaritySignalKind kind) {
    for (const auto& signal : wizard.polaritySignals()) {
        if (signal.kind == kind) return &signal;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("H7: the polarity-signal table is data, and a refusal is listed as a refusal",
          "[alignment_wizard]") {
    const auto buildWizard = [](OutputEngine& engine, rta::ir::Deconvolution high,
                                rta::ir::Deconvolution low) {
        auto wizard = std::make_unique<AlignmentWizard>(engine, pinkNoise(),
                                                        rta::test::makeConfig());
        wizard->answerHighPassSide(Source::Main);
        wizard->answerTopology({ CrossoverFamily::LinkwitzRiley, 4 });
        wizard->answerInversion(ProcessorInversion::No);
        wizard->answerCrossoverSeed(std::nullopt);
        wizard->supplyImpulseResponses(std::move(high), std::move(low));
        wizard->supplyWhitenedPeakSign(rta::ir::Sign::Positive);
        runSequence(*wizard, engine,
                    rta::test::makeCapture("hp", rta::test::risingDb(),
                                           rta::test::constantPhase(0.0)),
                    rta::test::makeCapture("lp", rta::test::fallingDb(),
                                           rta::test::constantPhase(0.0)));
        return wizard;
    };

    SECTION("a full-range pair: both answer, both are listed, neither is authoritative") {
        OutputEngine engine;
        engine.prepare(kFs, rta::test::kChannels);
        auto wizard = buildWizard(engine, measureThrough(60.0, 15000.0, 4, +1.0f),
                                  measureThrough(60.0, 15000.0, 4, -1.0f));

        const auto* high = signalOf(*wizard, PolaritySignalKind::FindPolarityHighSide);
        const auto* low = signalOf(*wizard, PolaritySignalKind::FindPolarityLowSide);
        const auto* rho = signalOf(*wizard, PolaritySignalKind::RelativePolarityRho);
        REQUIRE(high != nullptr);
        REQUIRE(low != nullptr);
        REQUIRE(rho != nullptr);
        CHECK(high->sign == rta::ir::Sign::Positive);
        CHECK(low->sign == rta::ir::Sign::Negative);
        CHECK(rho->sign == rta::ir::Sign::Negative);
        CHECK(rho->figure > 0.9);
        // Both agree that the pair is relatively inverted, so nothing is asked.
        CHECK(wizard->step() != WizardStep::AskingPolarity);
        for (const auto& signal : wizard->polaritySignals()) {
            CHECK(signal.standing != SignalStanding::Authoritative);
        }
        CHECK(rho->reason == std::string(rta::measure::acrossCrossoverReason()));
    }

    SECTION("two eligible signals that disagree put the wizard in AskingPolarity") {
        OutputEngine engine;
        engine.prepare(kFs, rta::test::kChannels);
        auto wizard = buildWizard(engine, measureThrough(60.0, 15000.0, 4, +1.0f),
                                  twoWayWithInvertedTweeter(2000.0));

        const auto* low = signalOf(*wizard, PolaritySignalKind::FindPolarityLowSide);
        const auto* rho = signalOf(*wizard, PolaritySignalKind::RelativePolarityRho);
        REQUIRE(low != nullptr);
        REQUIRE(rho != nullptr);
        INFO("findPolarity(low) " << static_cast<int>(low->sign) << ", rho sign "
                                  << static_cast<int>(rho->sign) << " at " << rho->figure);
        CHECK(low->sign == rta::ir::Sign::Negative);   // the first wavefront IS inverted
        CHECK(rho->sign == rta::ir::Sign::Positive);   // the window's energy is not
        CHECK(wizard->step() == WizardStep::AskingPolarity);
    }

    SECTION("a subwoofer's refusal is listed, and nothing is promoted to fill it") {
        OutputEngine engine;
        engine.prepare(kFs, rta::test::kChannels);
        auto wizard = buildWizard(engine, measureThrough(60.0, 15000.0, 4, +1.0f),
                                  measureThrough(30.0, 90.0, 4, +1.0f));

        const auto* low = signalOf(*wizard, PolaritySignalKind::FindPolarityLowSide);
        REQUIRE(low != nullptr);
        // Polarity.h:30, by design: a subwoofer has no high edge to give the
        // wavefront a front.
        CHECK(low->refusal == rta::ir::Refusal::BandTooHigh);
        CHECK(low->standing == SignalStanding::Refused);

        // The whitened peak was supplied and is STILL only a witness. Record
        // Sec.7's table, row 2: `inverted` is never promoted to fill the gap.
        const auto* whitened = signalOf(*wizard, PolaritySignalKind::WhitenedCorrelationPeak);
        REQUIRE(whitened != nullptr);
        CHECK(whitened->standing == SignalStanding::Advisory);
        CHECK(whitened->reason == std::string(rta::measure::acrossCrossoverReason()));
        // With one side refused there is no second eligible signal, so there is
        // nothing to disagree with -- and nothing is chosen either.
        CHECK(wizard->step() != WizardStep::AskingPolarity);
    }
}

TEST_CASE("H8: rho's sign cannot reach the verdict -- an invariance, not a grep",
          "[alignment_wizard]") {
    const auto run = [](OutputEngine& engine, bool negateLowSide) {
        auto high = measureThrough(60.0, 15000.0, 4, +1.0f);
        auto low = measureThrough(60.0, 15000.0, 4, +1.0f);
        if (negateLowSide) {
            for (auto& sample : low.samples) sample = -sample;
        }
        auto wizard = std::make_unique<AlignmentWizard>(engine, pinkNoise(),
                                                        rta::test::makeConfig());
        wizard->answerHighPassSide(Source::Main);
        wizard->answerTopology({ CrossoverFamily::Butterworth, 2 });
        wizard->answerInversion(ProcessorInversion::No);
        wizard->answerCrossoverSeed(std::nullopt);
        wizard->supplyImpulseResponses(std::move(high), std::move(low));
        runSequence(*wizard, engine,
                    rta::test::makeCapture("hp", rta::test::risingDb(),
                                           rta::test::constantPhase(0.7)),
                    rta::test::makeCapture("lp", rta::test::fallingDb(),
                                           rta::test::constantPhase(0.0)));
        return wizard;
    };

    OutputEngine engineA;
    engineA.prepare(kFs, rta::test::kChannels);
    auto plain = run(engineA, false);
    OutputEngine engineB;
    engineB.prepare(kFs, rta::test::kChannels);
    auto flipped = run(engineB, true);

    REQUIRE(plain->verdict().has_value());
    REQUIRE(flipped->verdict().has_value());
    const auto& a = *plain->verdict();
    const auto& b = *flipped->verdict();

    // FIELD FOR FIELD. Feeding any time-domain sign into the verdict breaks
    // this, which is what makes the claim red-able where a file-scoped grep
    // could not be: H7 requires polaritySignals() to read that same sign in
    // the same file.
    CHECK(a.crossoverHz == b.crossoverHz);
    CHECK(a.tauSeconds == b.tauSeconds);
    CHECK(a.interceptRadians == b.interceptRadians);
    CHECK(a.agreement == b.agreement);
    CHECK(a.meanFrequencyHz == b.meanFrequencyHz);
    CHECK(a.expectedRadians == b.expectedRadians);
    CHECK(a.deltaRadians == b.deltaRadians);
    CHECK(a.expectedAmbiguous == b.expectedAmbiguous);
    CHECK(a.alternativeExpectedRadians == b.alternativeExpectedRadians);
    CHECK(a.alternativeDeltaRadians == b.alternativeDeltaRadians);
    CHECK(a.matchedPair == b.matchedPair);
    CHECK(a.fitRefusal == b.fitRefusal);

    // And the signals DO differ -- otherwise the invariance would be vacuous.
    const auto* rhoPlain = signalOf(*plain, PolaritySignalKind::RelativePolarityRho);
    const auto* rhoFlipped = signalOf(*flipped, PolaritySignalKind::RelativePolarityRho);
    REQUIRE(rhoPlain != nullptr);
    REQUIRE(rhoFlipped != nullptr);
    CHECK(rhoPlain->sign != rhoFlipped->sign);
}

TEST_CASE("H1/H6/H10 structurally: the asked answers have exactly four writers, and no "
          "whitened correlator lives here",
          "[alignment_wizard]") {
    const std::filesystem::path root{ RTA_REPO_ROOT };
    const std::filesystem::path sources[]{
        root / "app" / "src" / "measure" / "AlignmentWizard.cpp",
        root / "app" / "src" / "measure" / "AlignmentWizardSignals.cpp",
    };
    const std::string guarded[]{ "topology_", "inversion_", "highpassside_", "seedhz_",
                                 "seedanswered_", "cycleanswer_" };
    const std::string forbidden[]{ "finddelayphat", "suggestdelay", "maximis", "maximiz",
                                   "optimis",       "optimiz",      "minimis", "minimiz" };

    std::size_t writesSeen = 0;
    std::size_t functionsSeen = 0;
    std::vector<std::string> offenders;

    for (const auto& source : sources) {
        REQUIRE(std::filesystem::exists(source));
        std::string currentFunction;
        for (const auto& line : codeLines(source)) {
            const auto scope = line.find("alignmentwizard::");
            if (scope != std::string::npos && line.find('(') != std::string::npos
                && line.rfind("    ", 0) != 0) {
                currentFunction = line.substr(scope + std::string("alignmentwizard::").size());
                currentFunction = currentFunction.substr(0, currentFunction.find('('));
                ++functionsSeen;
            }
            for (const auto& member : guarded) {
                // An ASSIGNMENT, not a comparison. `highPassSide_ == Source::Main`
                // begins with `highPassSide_ =` and is a READ -- the scan has to
                // tell the two apart or it reports five offenders that are the
                // code doing exactly what it should.
                const auto at = line.find(member + " =");
                if (at == std::string::npos) continue;
                const auto after = at + member.size() + 2;
                if (after < line.size() && line[after] == '=') continue;
                ++writesSeen;
                // The ONE rule: an asked answer is written by an answer*()
                // setter and by nothing else. A wizard that set the inversion
                // answer from the sign of its own fitted intercept would have
                // made the forbidden move, and this is what stops it landing.
                if (currentFunction.rfind("answer", 0) != 0) {
                    offenders.push_back(source.filename().string() + " :: " + currentFunction
                                        + " -> " + line);
                }
            }
            for (const auto& word : forbidden) {
                if (line.find(word) != std::string::npos) {
                    offenders.push_back(source.filename().string() + " :: " + line);
                }
            }
            // H5: the wizard applies NO rotation of its own -- the
            // appliedDelaySamples correction is core's, by ALIGN-R2.
            if (line.find("exp(") != std::string::npos) {
                offenders.push_back(source.filename().string() + " :: exp -> " + line);
            }
        }
    }

    // Accumulated into ONE string rather than one INFO per offender: an INFO
    // inside a loop goes out of scope with the iteration, so the list would be
    // gone by the time the check below runs -- a failure with nothing to read.
    std::string report;
    for (const auto& offender : offenders) report += "\n  " + offender;
    INFO("writes to guarded members seen: " << writesSeen << "; functions seen: " << functionsSeen
                                            << "; offenders:" << report);
    // The scan is watching something: if it stopped matching, these two would
    // collapse and the case would go red before the emptiness check does.
    CHECK(writesSeen >= 6u);
    CHECK(functionsSeen >= 8u);
    CHECK(offenders.empty());
}
