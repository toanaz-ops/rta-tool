// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ALIGN-UI lane wave A, T2 -- the words the runner says and the early exits no
// other file drove. `tools/diffmut.py` deleted every added line of
// AlignRunner.cpp in turn and these are the lines it found nothing watching:
// the named missing answer, the refusals of MEASURE and SUM, what the status
// line says at each step, the abort that needs the SYNTHETIC flag the other
// way round, the wait that gives up, and the refusals of the wizard and of
// the fit. Rig and physics are AlignRunnerFixture.h's.

#include "AlignRunnerFixture.h"

#include <catch2/catch_test_macros.hpp>

#include "rta/gen/Noise.h"
#include "rta/gen/Prng.h"

#include <string>

using alignfixture::Rig;
using rta::measure::AlignBlock;
using rta::measure::AlignOutcome;
using rta::measure::Source;
using rta::measure::WizardStep;

namespace {

bool said(const Rig& rig, const std::string& needle) {
    return rig.runner.status().find(needle) != std::string::npos;
}

/// Renders past the dwell of the CURRENT step without letting the clock move.
void renderPastDwell(Rig& rig) { rig.pump(static_cast<int>(rig.turnoverSamples() / alignfixture::kBlock) + 4); }

}  // namespace

TEST_CASE("S1: the missing answer is named, for each of the four questions", "[align_runner_status]") {
    const auto missing = [](auto clear, const std::string& word) {
        Rig rig;
        rig.answerAll();
        clear(rig.runner.answers());
        const auto blocker = rig.runner.blocker();
        CHECK(blocker.kind == AlignBlock::MissingAnswer);
        CHECK(blocker.text.find(word) != std::string::npos);
    };
    missing([](auto& a) { a.highPass.reset(); }, "high-pass side");
    missing([](auto& a) { a.topology.reset(); }, "topology");
    missing([](auto& a) { a.inversion.reset(); }, "inversion");
    missing([](auto& a) { a.seedAnswered = false; }, "crossover seed");
}

TEST_CASE("S2: MEASURE is refused while another source holds the engine", "[align_runner_status]") {
    Rig rig;
    rig.answerAll();
    REQUIRE(rig.engine.setSource(rta::gen::PinkNoise(rta::gen::Pcg32(1u, 1), -12.0)));
    rig.engine.armSource();
    CHECK(rig.runner.blocker().kind == AlignBlock::EngineNotQuiescent);
    CHECK_FALSE(rig.runner.press());
    CHECK(rig.runner.lastRefusalForTest() == AlignBlock::EngineNotQuiescent);
}

TEST_CASE("S3: what the status line says at each step, and that each change moves the revision", "[align_runner_status]") {
    Rig rig;
    rig.answerAll();
    const auto r0 = rig.runner.revision();
    REQUIRE(rig.runner.press());
    CHECK(rig.runner.revision() > r0);
    CHECK(said(rig, "pink noise on MAIN (OUT 1) only"));
    CHECK(rig.runner.sampleRate() == alignfixture::kFs);
    CHECK(rig.runner.runAnswers().highPass == std::optional<Source>(Source::Main));

    // Re-answering for the NEXT run does not touch the one in flight.
    rig.runner.answers().highPass = Source::Sub;
    CHECK(rig.runner.runAnswers().highPass == std::optional<Source>(Source::Main));
    CHECK(std::as_const(rig.runner).answers().highPass == std::optional<Source>(Source::Sub));
    rig.runner.answers().highPass = Source::Main;

    REQUIRE(rig.pumpUntil([&] { return rig.runner.step() == WizardStep::MeasuringLow; }));
    CHECK(said(rig, "captured; now pink noise on SUB (OUT 2) only"));
    REQUIRE(rig.pumpUntil([&] { return !rig.runner.busy(); }));
    rig.pump(8);

    const auto r1 = rig.runner.revision();
    CHECK_FALSE(rig.runner.answerCycleMs(std::nan("")));
    CHECK(said(rig, "OFFSET: run MEASURE first"));
    REQUIRE(rig.runner.answerCycleMs(2.0));
    CHECK(said(rig, "coarse offset +2.00 ms = +96 samples accepted"));
    CHECK(rig.runner.revision() > r1);

    REQUIRE(rig.runner.pressSum());
    CHECK(said(rig, "pink noise on MAIN and SUB together"));
    REQUIRE(rig.pumpUntil([&] { return !rig.runner.busy(); }));
    CHECK(said(rig, "measured sum stored as ALIGN SUM"));
}

TEST_CASE("S4: a second press clears the last run's refusal, outcome and offset", "[align_runner_status]") {
    Rig rig;
    rig.answerAll();
    rig.env.deviceRunning = false;
    CHECK_FALSE(rig.runner.press());
    CHECK(rig.runner.lastRefusalForTest() == AlignBlock::DeviceNotRunning);
    rig.env.deviceRunning = true;
    REQUIRE(rig.runToVerdict());
    REQUIRE(rig.runner.answerCycleMs(2.0));
    CHECK(rig.runner.lastRefusalForTest() == AlignBlock::None);
    CHECK(rig.runner.outcomeForTest() == AlignOutcome::Measured);
    REQUIRE(rig.runner.cycleSamples().has_value());

    REQUIRE(rig.runner.press());
    CHECK(rig.runner.outcomeForTest() == AlignOutcome::None);
    CHECK_FALSE(rig.runner.cycleSamples().has_value());
}

TEST_CASE("S5: HP = SUB says OUT 2 first", "[align_runner_status]") {
    Rig rig;
    rig.answerAll(Source::Sub);
    REQUIRE(rig.runner.press());
    CHECK(said(rig, "pink noise on SUB (OUT 2) only"));
}

TEST_CASE("S6: SUM refusals are named and leave the run where it was", "[align_runner_status]") {
    Rig rig;
    rig.answerAll();
    REQUIRE(rig.runToVerdict());
    CHECK_FALSE(rig.runner.pressSum());  // the offset is still unanswered
    CHECK(said(rig, "SUM: answer the coarse offset first"));
    REQUIRE(rig.runner.answerCycleMs(2.0));

    rig.env.deviceRunning = false;
    CHECK_FALSE(rig.runner.pressSum());
    CHECK(said(rig, "SUM refused: the audio device is not running"));
    rig.env.deviceRunning = true;

    REQUIRE(rig.engine.setSource(rta::gen::PinkNoise(rta::gen::Pcg32(1u, 1), -12.0)));
    rig.engine.armSource();
    CHECK_FALSE(rig.runner.pressSum());
    CHECK(said(rig, "SUM refused: the output engine is not quiescent"));
    CHECK(rig.runner.step() == WizardStep::Computed);
}

TEST_CASE("S7: a SYNTHETIC run aborts when SYNTHETIC is switched OFF, and a changed averaging mode aborts",
          "[align_runner_status]") {
    SECTION("SYNTHETIC on at the press, off mid-run") {
        Rig rig;
        rig.answerAll();
        rig.env.synthetic = true;
        REQUIRE(rig.runner.press());
        rig.pump(2);
        rig.env.synthetic = false;
        rig.pump(1);
        CHECK(rig.runner.outcomeForTest() == AlignOutcome::OutputChanged);
        CHECK_FALSE(rig.runner.busy());
    }
    SECTION("SYNTHETIC on during a SUM press, off mid-capture") {
        Rig rig;
        rig.answerAll();
        rig.env.synthetic = true;
        REQUIRE(rig.runToVerdict());
        REQUIRE(rig.runner.answerCycleMs(2.0));
        REQUIRE(rig.runner.pressSum());
        rig.pump(2);
        rig.env.synthetic = false;
        rig.pump(1);
        CHECK(rig.runner.outcomeForTest() == AlignOutcome::OutputChanged);
    }
    SECTION("averaging mode changed mid-run") {
        Rig rig;
        rig.answerAll();
        REQUIRE(rig.runner.press());
        rig.pump(2);
        rig.config.transferAveraging = rta::dsp::TransferAveraging::Exponential;
        renderPastDwell(rig);
        CHECK(rig.runner.outcomeForTest() == AlignOutcome::SettingsChanged);
        CHECK(said(rig, "averaging mode changed"));
    }
}

TEST_CASE("S8: a wait that never gets a usable snapshot gives up, saying so, and only after the timeout",
          "[align_runner_status]") {
    SECTION("the window has not opened") {
        Rig rig;
        rig.answerAll();
        REQUIRE(rig.runner.press());
        rig.pump(2);
        CHECK(rig.runner.busy());  // waiting is not failing
        rig.nowMs += 1.0e6;
        rig.pump(1);
        CHECK(rig.runner.outcomeForTest() == AlignOutcome::TimedOut);
        CHECK(said(rig, "ALIGN timed out"));
    }
    SECTION("no coherence after the dwell") {
        Rig rig;
        rig.answerAll();
        auto snapshot = alignfixture::makeSnapshot(1);
        const_cast<rta::measure::Snapshot&>(*snapshot).transfer->coherence.reset();
        rig.forcedSnapshot = snapshot;
        REQUIRE(rig.runner.press());
        renderPastDwell(rig);
        CHECK(rig.runner.busy());
        rig.nowMs += 1.0e6;
        rig.pump(1);
        CHECK(rig.runner.outcomeForTest() == AlignOutcome::TimedOut);
    }
    SECTION("the freeze produces nothing") {
        Rig rig;
        rig.answerAll();
        rig.freezeOverride = [](const rta::measure::Snapshot&) -> std::optional<rta::trace::Trace> {
            return std::nullopt;
        };
        REQUIRE(rig.runner.press());
        renderPastDwell(rig);
        CHECK(rig.runner.busy());
        rig.nowMs += 1.0e6;
        rig.pump(1);
        CHECK(rig.runner.outcomeForTest() == AlignOutcome::TimedOut);
    }
}

TEST_CASE("S9: the wizard's refusal of a capture and the fit's refusal are said, not swallowed",
          "[align_runner_status]") {
    SECTION("the second capture carries other channel roles") {
        Rig rig;
        rig.answerAll();
        int calls = 0;
        rig.freezeOverride = [&](const rta::measure::Snapshot& s) {
            auto trace = rta::trace::traceFromSnapshot(s, rta::view::PaneView::Transfer, rig.config, "TEST",
                                                       ++calls == 1 ? "ch0=Measurement,ch1=Reference"
                                                                    : "ch0=Reference,ch1=Measurement");
            return trace;
        };
        REQUIRE(rig.runner.press());
        REQUIRE(rig.pumpUntil([&] { return !rig.runner.busy(); }));
        CHECK(rig.runner.outcomeForTest() == AlignOutcome::SettingsChanged);
        CHECK(said(rig, "the wizard refused the capture"));
        CHECK(rig.runner.wizardRefusalForTest() == rta::measure::WizardRefusal::ReferenceMismatch);
    }
    SECTION("two magnitudes that never cross") {
        Rig rig;
        rig.answerAll();
        int calls = 0;
        rig.freezeOverride = [&](const rta::measure::Snapshot& s) {
            rta::measure::Snapshot shifted = s;
            const float offset = ++calls == 1 ? 0.0f : -30.0f;
            for (auto& db : shifted.transfer->magnitudeDb) db = offset;
            return rta::trace::traceFromSnapshot(shifted, rta::view::PaneView::Transfer, rig.config, "TEST",
                                                 "ch0=Measurement,ch1=Reference");
        };
        REQUIRE(rig.runner.press());
        REQUIRE(rig.pumpUntil([&] { return !rig.runner.busy(); }));
        CHECK(rig.runner.outcomeForTest() == AlignOutcome::FitRefused);
        CHECK(said(rig, "the fit refused"));
        CHECK(said(rig, "-- no suggestion"));
    }
}

TEST_CASE("S10: SUM starts its own run: it re-reads the epoch and the input mode, and clears the outcome",
          "[align_runner_status]") {
    Rig rig;
    rig.answerAll();
    REQUIRE(rig.runToVerdict());
    REQUIRE(rig.runner.answerCycleMs(2.0));
    CHECK(rig.runner.outcomeForTest() == AlignOutcome::Measured);

    // Between MEASURE and SUM the device is reopened and SYNTHETIC is turned on:
    // neither is a change DURING the SUM run, so neither may abort it.
    rig.engine.prepare(alignfixture::kFs, alignfixture::kChannels);
    rig.env.synthetic = true;
    REQUIRE(rig.runner.pressSum());
    CHECK(rig.runner.outcomeForTest() == AlignOutcome::None);  // the old run's outcome is gone
    rig.pump(2);
    CHECK(rig.runner.busy());
    REQUIRE(rig.pumpUntil([&] { return !rig.runner.busy(); }));
    CHECK(rig.runner.outcomeForTest() == AlignOutcome::SumMeasured);
}
