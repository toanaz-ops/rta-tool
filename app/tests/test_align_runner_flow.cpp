// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ALIGN-UI lane wave A, task T2 (docs/plans/2026-10-20-align-ui-lane-plan.md,
// decisions 6, 8): the RUN -- what ends it early and says so, the verdict a
// finished one produces, the coarse offset, and SUM. The refusals, the routing
// and the dwell are test_align_runner.cpp. The rig is AlignRunnerFixture.h's.
//
// Bounds below are discrimination bounds, not accuracy claims (accuracy is
// core C1's job): a swapped solo order gives tau = -2 ms and a transposed
// answer flips its sign, so the right answer and the wrong one are 4 ms apart
// and the bound is half of that.

#include "AlignRunnerFixture.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <string>

using alignfixture::kBlock;
using alignfixture::kFs;
using alignfixture::Rig;
using Catch::Matchers::WithinAbs;
using rta::measure::AlignBlock;
using rta::measure::AlignOutcome;
using rta::measure::Source;
using rta::measure::WizardStep;
using rta::platform::OutputRole;

namespace {

bool routed(const Rig& rig, int channel) { return rig.engine.role(channel) == OutputRole::Routed; }

/// Pumps long enough for an aborted excitation's ramp-out to finish.
bool quietsDown(Rig& rig) {
    rig.renderOnly(8);
    return rig.engine.sourceIsQuiescent();
}

}  // namespace

TEST_CASE("T2e: anything that moves mid-run aborts it, disarms, and says why", "[align_runner_flow]") {
    const auto startRun = [](Rig& rig) {
        rig.answerAll();
        REQUIRE(rig.runner.press());
        rig.pump(2);
        REQUIRE(rig.runner.busy());
    };
    const auto aborted = [](Rig& rig, AlignOutcome why, const std::string& needle) {
        CHECK(rig.runner.outcomeForTest() == why);
        CHECK_FALSE(rig.runner.busy());
        CHECK(rig.runner.status().find(needle) != std::string::npos);
        CHECK(quietsDown(rig));  // the loudspeaker is not left playing
        CHECK(rig.library.entries().empty());
    };

    SECTION("the output epoch moved (a device switch)") {
        Rig rig;
        startRun(rig);
        rig.engine.prepare(kFs, alignfixture::kChannels);  // what a device reopen does
        rig.runner.poll();
        aborted(rig, AlignOutcome::OutputChanged, "reopened");
    }
    SECTION("SYNTHETIC was toggled") {
        Rig rig;
        startRun(rig);
        rig.env.synthetic = true;  // SYNTHETIC turned ON mid-run
        rig.runner.poll();
        aborted(rig, AlignOutcome::OutputChanged, "SYNTHETIC");
    }
    SECTION("the device stopped") {
        Rig rig;
        startRun(rig);
        rig.env.deviceRunning = false;
        rig.runner.poll();
        aborted(rig, AlignOutcome::DeviceStopped, "device stopped");
    }
    SECTION("no usable snapshot within the dwell plus 5000 ms") {
        Rig rig;
        startRun(rig);
        rig.nullSnapshot = true;
        // Exactly the limit is still allowed; one millisecond past it is not
        // (captureTimedOut compares with >). The clock is the run's own: the
        // dwell in milliseconds plus the stated 5000.
        const double dwellMs = 1000.0 * static_cast<double>(rig.turnoverSamples()) / kFs;
        rig.nowMs += dwellMs + 5000.0;
        rig.runner.poll();
        CHECK(rig.runner.busy());
        rig.nowMs += 1.0;
        rig.runner.poll();
        aborted(rig, AlignOutcome::TimedOut, "timed out");
    }
    SECTION("the transfer grid or averaging mode changed") {
        Rig rig;
        startRun(rig);
        rig.config.transferAveraging = rta::dsp::TransferAveraging::Exponential;
        rig.pump(140);  // past the dwell: it would submit if it had not noticed
        aborted(rig, AlignOutcome::SettingsChanged, "averaging");
    }
    SECTION("the fftSize changed") {
        Rig rig;
        startRun(rig);
        rig.forcedSnapshot = alignfixture::makeSnapshot(1, 2048);
        rig.pump(140);
        aborted(rig, AlignOutcome::SettingsChanged, "fftSize");
    }
}

TEST_CASE("T2f: a finished run asks for the coarse offset, and 2.0 ms at 48 kHz is 96 samples",
          "[align_runner_flow]") {
    Rig rig;
    rig.answerAll();
    REQUIRE(rig.runToVerdict());
    REQUIRE(rig.runner.verdict().has_value());

    // No IR pair exists in wave A, so the cycle integer has no source and no
    // answer: the wizard stops and asks.
    CHECK(rig.runner.step() == WizardStep::AskingCycle);
    CHECK_FALSE(rig.runner.cycleSamples().has_value());
    CHECK(rig.runner.status().find("coarse offset") != std::string::npos);
    CHECK_FALSE(rig.runner.pressSum());  // not before the offset is answered
    CHECK(rig.runner.status().find("coarse offset") != std::string::npos);

    CHECK_FALSE(rig.runner.answerCycleMs(std::nan("")));
    REQUIRE(rig.runner.answerCycleMs(2.0));
    CHECK(rig.runner.cycleSamples() == std::optional<std::ptrdiff_t>(96));
    CHECK(rig.runner.step() == WizardStep::Computed);

    // The verdict: LP arrives 96 samples (2.000 ms) after HP, LR-4 not
    // inverted. Printed beside their bounds; the bounds are discrimination
    // bounds (a swapped order reads -2 ms, a flipped inversion reads 180).
    const auto& v = *rig.runner.verdict();
    WARN("T2f tau = " << v.tauSeconds * 1e3 << " ms (bound 2 ms), delta = "
                      << v.deltaRadians * 180.0 / 3.14159265358979 << " deg (bound 90), R = " << v.agreement
                      << ", crossover " << v.crossoverHz << " Hz, " << v.candidateTausSeconds.size()
                      << " candidates");
    CHECK_THAT(v.tauSeconds, WithinAbs(2.0e-3, 2.0e-3));
    CHECK(std::abs(v.deltaRadians) < 3.14159265358979 / 2.0);
    CHECK(v.fitRefusal == rta::dsp::CrossoverRefusal::None);
    CHECK(v.candidateTausSeconds.size() >= 2);
}

TEST_CASE("T2g: a second MEASURE replays the answers and starts a fresh run", "[align_runner_flow]") {
    Rig rig;
    rig.answerAll(Source::Sub);
    rig.runner.answers().mainOutput = 1;  // SUB is the high-pass side: output 0 plays it first
    rig.runner.answers().subOutput = 0;
    REQUIRE(rig.runToVerdict());
    const auto before = rig.runner.answers();
    REQUIRE(rig.library.entries().size() == 2);

    REQUIRE(rig.runner.press());
    CHECK(rig.runner.step() == WizardStep::MeasuringHigh);
    CHECK_FALSE(rig.runner.verdict().has_value());  // the old verdict does not survive into the new run
    CHECK(rig.runner.answers().highPass == before.highPass);
    CHECK(rig.runner.answers().topology->order == before.topology->order);
    CHECK(rig.runner.answers().inversion == before.inversion);
    CHECK(rig.runner.answers().seedAnswered);
    CHECK(routed(rig, 0));  // the replayed answer still puts SUB (output 0) first
    REQUIRE(rig.pumpUntil([&] { return !rig.runner.busy(); }));
    CHECK(rig.library.entries().size() == 4);  // both runs' captures are kept
}

TEST_CASE("T2h: SUM routes both outputs, waits out the dwell, and stores ALIGN SUM", "[align_runner_flow]") {
    Rig rig;
    rig.answerAll();
    CHECK_FALSE(rig.runner.pressSum());  // nothing measured yet
    CHECK(rig.runner.status().find("run MEASURE first") != std::string::npos);

    REQUIRE(rig.runToVerdict());
    REQUIRE(rig.runner.answerCycleMs(2.0));
    REQUIRE(rig.runner.pressSum());
    CHECK(rig.runner.step() == WizardStep::MeasuringSum);
    CHECK(rig.runner.busy());
    // Both outputs together, not main alone and not a strict solo.
    CHECK(routed(rig, 0));
    CHECK(routed(rig, 1));

    const std::size_t before = rig.library.entries().size();
    const std::uint64_t soloAt = rig.totalRendered;
    while (rig.totalRendered + kBlock < soloAt + rig.turnoverSamples()) rig.pump(1);
    CHECK(rig.library.entries().size() == before);  // not before the dwell since SUM began
    REQUIRE(rig.pumpUntil([&] { return !rig.runner.busy(); }, 8));

    REQUIRE(rig.library.entries().size() == before + 1);
    const auto& sum = rig.library.entries().back();
    CHECK(sum.name == "ALIGN SUM @ 12:34:56");
    CHECK(sum.group == "ALIGN");
    CHECK(rig.runner.outcomeForTest() == AlignOutcome::SumMeasured);
    CHECK(rig.runner.step() == WizardStep::Done);
    REQUIRE(rig.runner.measuredSum() != nullptr);
    const auto stored = rig.runner.measuredSum()->field(rta::trace::Field::Magnitude);
    const auto expected = alignfixture::makeSnapshot(3)->transfer->magnitudeDb;  // both outputs
    for (std::size_t k = 0; k < stored.size(); ++k) REQUIRE(stored[k] == expected[k]);

    rig.pump(8);
    CHECK(rig.engine.sourceIsQuiescent());
    CHECK_FALSE(rig.runner.pressSum());  // one SUM per run
}

TEST_CASE("T2i: SUM refuses while another feature holds the output, and takes the output back first",
          "[align_runner_flow]") {
    Rig rig;
    rig.answerAll();
    REQUIRE(rig.runToVerdict());
    REQUIRE(rig.runner.answerCycleMs(2.0));
    // The verdict step leaves the last-soloed output routed (disarmed); a
    // refused SUM must not touch the routes in either direction.
    const bool routed0 = routed(rig, 0);
    const bool routed1 = routed(rig, 1);
    rig.env.captureBusy = true;
    CHECK_FALSE(rig.runner.pressSum());
    CHECK(rig.runner.status().find("SUM refused") != std::string::npos);
    CHECK(rig.runner.step() == WizardStep::Computed);
    CHECK(routed(rig, 0) == routed0);
    CHECK(routed(rig, 1) == routed1);
}
