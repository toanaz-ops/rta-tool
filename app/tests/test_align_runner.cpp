// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ALIGN-UI lane wave A, task T2 (docs/plans/2026-10-20-align-ui-lane-plan.md,
// decisions 3, 4, 8, 11): AlignRunner's refusals, the output each step plays
// on, and the dwell. The run's aborts, the verdict and SUM are
// test_align_runner_flow.cpp. The rig is AlignRunnerFixture.h's: `latest()`
// serves the room as it was BEFORE the last routing change until depth *
// fftSize samples have rendered since it, then the room for the routed set.

#include "AlignRunnerFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using alignfixture::kBlock;
using alignfixture::Rig;
using rta::measure::AlignBlock;
using rta::measure::AlignOutcome;
using rta::measure::Source;
using rta::measure::WizardStep;
using rta::platform::OutputRole;

namespace {

bool routed(const Rig& rig, int channel) { return rig.engine.role(channel) == OutputRole::Routed; }

/// What the runner said, in the operator's words.
bool said(const Rig& rig, const std::string& needle) {
    return rig.runner.status().find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("T2a: every refusal is named, says why, and leaves the engine untouched", "[align_runner]") {
    const auto refuses = [](Rig& rig, AlignBlock kind) {
        REQUIRE_FALSE(rig.runner.press());
        CHECK(rig.runner.lastRefusalForTest() == kind);
        CHECK(said(rig, "MEASURE refused: " + rta::measure::alignBlockText(kind)));
        CHECK_FALSE(rig.runner.busy());
        CHECK(rig.engine.sourceIsQuiescent());  // nothing was armed
        CHECK_FALSE(routed(rig, 0));
        CHECK_FALSE(routed(rig, 1));
    };

    SECTION("not attached") {
        rta::measure::AlignRunner bare;
        CHECK(bare.blocker().kind == AlignBlock::NotAttached);
        CHECK_FALSE(bare.press());
        CHECK(bare.lastRefusalForTest() == AlignBlock::NotAttached);
    }
    SECTION("a question unanswered names which one") {
        Rig rig;
        rig.answerAll();
        rig.runner.answers().inversion.reset();
        refuses(rig, AlignBlock::MissingAnswer);
        CHECK(rig.runner.blocker().text.find("inversion") != std::string::npos);
        CHECK(rig.runner.blocker().text.find("topology") == std::string::npos);
    }
    SECTION("no seed is an answer, never asked is not") {
        Rig rig;
        rig.answerAll();
        rig.runner.answers().seedAnswered = false;
        refuses(rig, AlignBlock::MissingAnswer);
        rig.runner.answers().seedAnswered = true;  // "NO SEED": answered, with no value
        CHECK(rig.runner.blocker().kind == AlignBlock::None);
    }
    SECTION("MAIN equal to SUB") {
        Rig rig;
        rig.answerAll();
        rig.runner.answers().subOutput = rig.runner.answers().mainOutput;
        refuses(rig, AlignBlock::SameOutput);
    }
    SECTION("an output the device does not have") {
        Rig rig;
        rig.answerAll();
        rig.env.outputChannels = 1;  // SUB is out 2
        refuses(rig, AlignBlock::OutputMissing);
    }
    SECTION("device stopped and not SYNTHETIC") {
        Rig rig;
        rig.answerAll();
        rig.env.deviceRunning = false;
        refuses(rig, AlignBlock::DeviceNotRunning);
    }
    SECTION("SYNTHETIC is allowed with the device stopped") {
        Rig rig;
        rig.answerAll();
        rig.env.deviceRunning = false;
        rig.env.synthetic = true;
        CHECK(rig.runner.blocker().kind == AlignBlock::None);
        CHECK(rig.runner.press());
    }
    SECTION("LOCATE, CAL, SWEEP or VERIFY busy") {
        Rig rig;
        rig.answerAll();
        rig.env.captureBusy = true;
        refuses(rig, AlignBlock::CaptureBusy);
    }
    SECTION("no TRANSFER measurement published") {
        Rig rig;
        rig.answerAll();
        rig.nullSnapshot = true;
        refuses(rig, AlignBlock::NoTransfer);
        rig.nullSnapshot = false;
        auto rtaOnly = std::make_shared<rta::measure::Snapshot>();
        rtaOnly->sampleRate = alignfixture::kFs;
        rig.forcedSnapshot = rtaOnly;  // published, but no reference was fed
        refuses(rig, AlignBlock::NoTransfer);
    }
    SECTION("input and output sample rates differ") {
        Rig rig;
        rig.answerAll();
        rig.forcedSnapshot = alignfixture::makeSnapshot(0, alignfixture::kFft, 44100.0);
        refuses(rig, AlignBlock::SampleRateMismatch);
    }
    SECTION("Exponential averaging has no defined dwell") {
        Rig rig;
        rig.answerAll();
        rig.config.transferAveraging = rta::dsp::TransferAveraging::Exponential;
        refuses(rig, AlignBlock::ExponentialAveraging);
    }
    SECTION("the output engine is playing something else") {
        Rig rig;
        rig.answerAll();
        REQUIRE(rig.engine.setSource(rta::gen::PinkNoise(rta::gen::Pcg32(7, 1), -12.0)));
        rig.engine.armSource();
        rig.renderOnly(1);
        REQUIRE_FALSE(rig.engine.sourceIsQuiescent());
        REQUIRE_FALSE(rig.runner.press());
        CHECK(rig.runner.lastRefusalForTest() == AlignBlock::EngineNotQuiescent);
        // The other feature's excitation is still its own.
        CHECK_FALSE(rig.engine.sourceIsQuiescent());
    }
    SECTION("a second press while a step runs") {
        Rig rig;
        rig.answerAll();
        REQUIRE(rig.runner.press());
        REQUIRE_FALSE(rig.runner.press());
        CHECK(rig.runner.lastRefusalForTest() == AlignBlock::Running);
        CHECK(rig.runner.busy());
    }
}

TEST_CASE("T2b: only the high-pass side's output plays first, then only the low-pass side's", "[align_runner]") {
    for (const Source highPass : { Source::Main, Source::Sub }) {
        INFO("high-pass side is " << (highPass == Source::Main ? "MAIN" : "SUB"));
        Rig rig;
        rig.answerAll(highPass);
        // Outputs deliberately NOT in index order: a runner that soloed output
        // 0 first, or MAIN first, would pass the default 0/1 mapping unnoticed.
        rig.runner.answers().mainOutput = 1;
        rig.runner.answers().subOutput = 0;
        const int firstOutput = highPass == Source::Main ? 1 : 0;
        const int secondOutput = 1 - firstOutput;

        REQUIRE(rig.runner.press());
        CHECK(rig.runner.step() == WizardStep::MeasuringHigh);
        CHECK(routed(rig, firstOutput));
        CHECK_FALSE(routed(rig, secondOutput));

        REQUIRE(rig.pumpUntil([&] { return rig.runner.step() == WizardStep::MeasuringLow; }));
        CHECK(routed(rig, secondOutput));
        CHECK_FALSE(routed(rig, firstOutput));

        REQUIRE(rig.pumpUntil([&] { return !rig.runner.busy(); }));
        // The excitation stops the instant its job is done.
        rig.pump(8);
        CHECK(rig.engine.sourceIsQuiescent());
    }
}

TEST_CASE("T2c: nothing is submitted before the dwell since THIS step's solo, and never the stale room",
          "[align_runner]") {
    Rig rig;
    rig.answerAll();
    REQUIRE(rig.runner.press());
    const std::uint64_t turnover = rig.turnoverSamples();
    REQUIRE(turnover == 65536);  // 16 * 4096: Analyser.h's own default, not typed here

    // Right up to the last block before the first dwell has elapsed: the
    // window has long been open, the published snapshot is the flat room the
    // routing change has not yet reached.
    while (rig.totalRendered + kBlock < turnover) rig.pump(1);
    CHECK(rig.runner.step() == WizardStep::MeasuringHigh);
    CHECK(rig.freezes == 0);
    CHECK(rig.library.entries().empty());

    REQUIRE(rig.pumpUntil([&] { return rig.runner.step() == WizardStep::MeasuringLow; }, 4));
    REQUIRE(rig.library.entries().size() == 1);
    const auto first = rig.library.entries()[0];
    const auto* firstTrace = rig.library.trace(first.traceId);
    REQUIRE(firstTrace != nullptr);
    const auto expectedFirst = alignfixture::makeSnapshot(1)->transfer->magnitudeDb;  // output 0 alone
    const auto storedFirst = firstTrace->field(rta::trace::Field::Magnitude);
    REQUIRE(storedFirst.size() == expectedFirst.size());
    for (std::size_t k = 0; k < storedFirst.size(); ++k) REQUIRE(storedFirst[k] == expectedFirst[k]);

    // The SECOND step. Well over one dwell has rendered since the arm, so a
    // runner that counted from the arm would submit on its very next poll --
    // and would be served the first output's room, because the FIFO has not
    // turned over since the solo moved.
    const std::uint64_t secondSolo = rig.totalRendered;  // the render count the solo moved at
    while (rig.totalRendered + kBlock < secondSolo + turnover) rig.pump(1);
    CHECK(rig.runner.step() == WizardStep::MeasuringLow);
    CHECK(rig.library.entries().size() == 1);
    rig.pump(3);
    REQUIRE(rig.library.entries().size() == 2);
    const auto* secondTrace = rig.library.trace(rig.library.entries()[1].traceId);
    REQUIRE(secondTrace != nullptr);
    const auto expectedSecond = alignfixture::makeSnapshot(2)->transfer->magnitudeDb;  // output 1 alone
    const auto storedSecond = secondTrace->field(rta::trace::Field::Magnitude);
    for (std::size_t k = 0; k < storedSecond.size(); ++k) REQUIRE(storedSecond[k] == expectedSecond[k]);
}

TEST_CASE("T2d: captures land in library group ALIGN, named for the output that played", "[align_runner]") {
    for (const Source highPass : { Source::Main, Source::Sub }) {
        Rig rig;
        rig.answerAll(highPass);
        // output 0 plays the high-pass path in the fixture, so MAIN is output
        // 0 when MAIN is the high-pass side and output 1 otherwise.
        rig.runner.answers().mainOutput = highPass == Source::Main ? 0 : 1;
        rig.runner.answers().subOutput = highPass == Source::Main ? 1 : 0;
        REQUIRE(rig.runToVerdict());
        REQUIRE(rig.library.entries().size() == 2);
        const auto& entries = rig.library.entries();
        // High-pass side first, always: it is the output the sequence solos first.
        CHECK(entries[0].name == std::string("ALIGN ") + (highPass == Source::Main ? "MAIN" : "SUB") + " @ 12:34:56");
        CHECK(entries[1].name == std::string("ALIGN ") + (highPass == Source::Main ? "SUB" : "MAIN") + " @ 12:34:56");
        CHECK(entries[0].group == "ALIGN");
        CHECK(entries[1].group == "ALIGN");
        CHECK(rig.runner.mainCapture() != nullptr);
        CHECK(rig.runner.subCapture() != nullptr);
        CHECK(rig.runner.outcomeForTest() == AlignOutcome::Measured);
    }
}
