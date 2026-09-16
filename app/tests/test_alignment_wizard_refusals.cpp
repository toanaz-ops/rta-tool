// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-ALIGN task H, the REFUSAL VOCABULARY (plan H4, extended). Split out of
// test_alignment_wizard.cpp on 2026-09-16 when adding PR #9's defect-D3
// coverage pushed that file to 401 lines -- one past CLAUDE.md's hard cap. The
// seam is a real one: the sequence file proves the L7-OUT steps happen in
// order, and this file proves that every way OUT of that sequence has a name.

#include "AlignmentWizardFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <vector>

using rta::measure::AlignmentWizard;
using rta::measure::CrossoverFamily;
using rta::measure::ProcessorInversion;
using rta::measure::Source;
using rta::measure::WizardRefusal;
using rta::measure::WizardStep;
using rta::platform::OutputEngine;
using rta::test::kChannels;
using rta::test::kFs;
using rta::test::pinkNoise;

namespace {

void answerEverything(AlignmentWizard& wizard) {
    wizard.answerHighPassSide(Source::Main);
    wizard.answerTopology({ CrossoverFamily::LinkwitzRiley, 4 });
    wizard.answerInversion(ProcessorInversion::No);
    wizard.answerCrossoverSeed(std::nullopt);
}

}  // namespace

TEST_CASE("H4b: EVERY named refusal is reachable, and each one is reached here",
          "[alignment_wizard]") {
    // Added 2026-09-16 after PR #9's verifier (defect D3): three of the nine
    // enumerators -- EngineNotQuiescent, WrongStep and TraceNotUsable -- had no
    // test. "Named refusals, each named, none silent" was demonstrated for six
    // of nine, and TraceNotUsable is a LIVE branch (a capture with no phase).
    //
    // WHAT STOPS A TENTH REFUSAL LANDING UNTESTED -- corrected 2026-09-16 after
    // the round-2 verifier REFUTED the first answer. That answer was "this
    // switch has no `default:`, so a tenth enumerator makes MSVC emit C4062 at
    // /W4 and the zero-warning gate catches it". Measured on MSVC 14.51: C4062
    // is OFF by default and `/W4` does not turn it on (`-W4` silent, `-W4
    // -w14062` warns), so a tenth enumerator built clean and this case passed.
    // A guard that is documented and absent is worse than none.
    //
    // What replaces it needs no warning flag and behaves identically on all
    // three CI operating systems: `WizardRefusal::Count` is a sentinel, and a
    // new enumerator goes ABOVE it. That moves `kWizardRefusalCount`, and the
    // check at the end of this case -- `seen.size() == kWizardRefusalCount` --
    // then goes red until the new refusal is actually driven and named here.
    //
    // The switch still has no `default:`, which is worth keeping for a second
    // reason: GCC and Clang's -Wall DOES include -Wswitch, so on the two CI
    // runners that are not Windows an unhandled enumerator is a build warning
    // as well. It is the backstop on two platforms out of three, and the
    // sentinel is the one that works on all three.
    const auto refusalName = [](WizardRefusal refusal) -> std::string {
        switch (refusal) {
            case WizardRefusal::Count: return "Count (not a refusal)";
            case WizardRefusal::None: return "None";
            case WizardRefusal::MissingAnswer: return "MissingAnswer";
            case WizardRefusal::EngineNotQuiescent: return "EngineNotQuiescent";
            case WizardRefusal::WindowNotOpen: return "WindowNotOpen";
            case WizardRefusal::WrongStep: return "WrongStep";
            case WizardRefusal::SampleRateMismatch: return "SampleRateMismatch";
            case WizardRefusal::FftSizeMismatch: return "FftSizeMismatch";
            case WizardRefusal::ReferenceMismatch: return "ReferenceMismatch";
            case WizardRefusal::TraceNotUsable: return "TraceNotUsable";
        }
        return "UNNAMED";
    };

    std::set<std::string> seen;
    const auto record = [&](const AlignmentWizard& wizard) {
        seen.insert(refusalName(wizard.lastRefusal()));
    };

    SECTION("drive every one of them") {
        // None, MissingAnswer, WindowNotOpen -- and WrongStep from arming twice.
        {
            OutputEngine engine;
            engine.prepare(kFs, kChannels);
            AlignmentWizard wizard(engine, pinkNoise(), rta::test::makeConfig());
            wizard.arm();
            record(wizard);  // MissingAnswer
            answerEverything(wizard);
            wizard.arm();
            record(wizard);  // None
            wizard.submitCapture(rta::test::makeCapture("early", rta::test::risingDb(),
                                                        rta::test::constantPhase(0.0)));
            record(wizard);  // WindowNotOpen
            CHECK(wizard.lastRefusal() == WizardRefusal::WindowNotOpen);
            wizard.arm();
            CHECK(wizard.lastRefusal() == WizardRefusal::WrongStep);
            record(wizard);  // WrongStep, from arming an already-armed wizard
        }

        // WrongStep again, from the other side: a measured sum before a fit.
        {
            OutputEngine engine;
            engine.prepare(kFs, kChannels);
            AlignmentWizard wizard(engine, pinkNoise(), rta::test::makeConfig());
            answerEverything(wizard);
            wizard.beginMeasuredSum();
            CHECK(wizard.lastRefusal() == WizardRefusal::WrongStep);
            CHECK(rta::test::routedCount(engine) == 0);  // nothing was routed
        }

        // EngineNotQuiescent: somebody else's excitation is already running, so
        // setSource says no. NOTHING is touched -- not the routing, not the
        // slot -- which is the half that matters: arming over a running source
        // would report a measurement of the wrong signal without saying so.
        {
            OutputEngine engine;
            engine.prepare(kFs, kChannels);
            REQUIRE(engine.setSource(pinkNoise()));
            engine.armSource();
            rta::test::pump(engine, 2);
            REQUIRE_FALSE(engine.sourceIsQuiescent());

            AlignmentWizard wizard(engine, pinkNoise(), rta::test::makeConfig());
            answerEverything(wizard);
            wizard.arm();
            CHECK(wizard.lastRefusal() == WizardRefusal::EngineNotQuiescent);
            CHECK(wizard.step() == WizardStep::Answering);
            CHECK(rta::test::routedCount(engine) == 0);
            record(wizard);
        }

        // TraceNotUsable: a capture with a magnitude and no phase has no
        // complex form, so there is nothing to fit. The wizard stops where it
        // was -- it does not invent a flat phase and it does not advance.
        {
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
            wizard.submitCapture(
                rta::test::makeCaptureWithoutPhase("lp", rta::test::fallingDb()));
            CHECK(wizard.lastRefusal() == WizardRefusal::TraceNotUsable);
            CHECK_FALSE(wizard.verdict().has_value());
            CHECK(wizard.step() == WizardStep::MeasuringLow);
            record(wizard);
        }

        // The three meta mismatches, reached by H4 above and repeated here only
        // so this case's own set is complete without depending on it.
        for (const auto& mismatch :
             { std::make_pair(WizardRefusal::SampleRateMismatch, 44100.0),
               std::make_pair(WizardRefusal::FftSizeMismatch, kFs),
               std::make_pair(WizardRefusal::ReferenceMismatch, kFs) }) {
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
            const int fftSize =
                mismatch.first == WizardRefusal::FftSizeMismatch ? 256 : rta::test::kFftSize;
            const char* roles = mismatch.first == WizardRefusal::ReferenceMismatch
                                    ? "ref:3 meas:4"
                                    : "ref:1 meas:2";
            const std::size_t points = rta::trace::pointCountFor(fftSize);
            wizard.submitCapture(rta::test::makeCapture("lp", std::vector<float>(points, -3.0f),
                                                        std::vector<float>(points, 0.0f), 0,
                                                        mismatch.second, fftSize, roles));
            CHECK(wizard.lastRefusal() == mismatch.first);
            record(wizard);
        }

        std::string sawList;
        for (const auto& name : seen) sawList += " " + name;
        INFO("refusals observed:" << sawList << " (against kWizardRefusalCount = "
                                  << rta::measure::kWizardRefusalCount << ")");
        CHECK(seen.count("UNNAMED") == 0u);
        // The sentinel is a count, never an answer: nothing may report it.
        CHECK(seen.count("Count (not a refusal)") == 0u);
        CHECK(seen.size() == rta::measure::kWizardRefusalCount);
    }
}

