// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-EQ UI wave B, tasks T7 and T8 (docs/plans/2026-09-29-eq-ui-lane-plan.md,
// D14): the RUN -- when a published snapshot may be trusted, what a stalled
// run does, that every press gets a fresh EqVerify -- and ADOPT. The refusals
// are test_eq_verify_runner.cpp. The physics is EqVerifyRunnerFixture.h's:
// `latest()` serves the pre-excitation room until depth * fftSize samples of
// excitation have rendered, then the room with the filters dialled in.
#include "EqVerifyRunnerFixture.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

using Catch::Approx;
using rta::measure::VerifyBlock;
using rta::measure::VerifyOutcome;
using rta::measure::VerifyState;
using verifyfixture::kBlock;
using verifyfixture::kFft;
using verifyfixture::Rig;

namespace {

/// depth * fftSize / fs, derived from Analyser.h's own default rather than
/// typed: 16 * 4096 / 48000 = 1.365 s.
double dwellSeconds() {
    return static_cast<double>(rta::measure::CaptureConfig{}.transferFifoDepth) * static_cast<double>(kFft) /
           eqfixture::kFs;
}

std::uint64_t dwellSamples() { return static_cast<std::uint64_t>(std::llround(dwellSeconds() * eqfixture::kFs)); }

}  // namespace

TEST_CASE("VERIFY: the dwell is depth * fftSize / fs, 1.365 s at the defaults (Analyser.h:48-49)") {
    CHECK(rta::measure::CaptureConfig{}.transferFifoDepth == 16);
    CHECK(dwellSeconds() == Approx(1.365).margin(0.001));
    CHECK(rta::measure::EqVerifyRunner::dwellSeconds(rta::measure::CaptureConfig{}, kFft, eqfixture::kFs) ==
          dwellSeconds());
    CHECK(dwellSamples() == 65536);
}

TEST_CASE("VERIFY never submits a snapshot earlier than the dwell after Measuring, and never the pre-excitation one") {
    Rig rig;
    REQUIRE(rig.runner.press());
    CHECK(rig.runner.state() == VerifyState::Waiting);

    rig.pump(1);  // 512 rendered: past the 10 ms (480 sample) settle, Measuring is observed here
    REQUIRE(rig.runner.state() == VerifyState::Measuring);
    const std::uint64_t measuringAt = rig.engine.renderedSamples();
    CHECK(measuringAt == static_cast<std::uint64_t>(kBlock));

    // Render right up to the last block BEFORE the dwell has elapsed since
    // Measuring. `latest()` already serves the post-excitation room at
    // rendered >= dwellSamples, so a runner counting the dwell from ARM (not
    // from Measuring) would submit here -- and one with no dwell long before.
    while (rig.engine.renderedSamples() + kBlock < measuringAt + dwellSamples()) rig.pump(1);
    CHECK(rig.engine.renderedSamples() >= dwellSamples());  // the post snapshot IS being served
    CHECK(rig.runner.state() == VerifyState::Measuring);
    CHECK_FALSE(rig.runner.report().has_value());
    CHECK(rig.freezes == 0);

    rig.pump(1);  // the dwell since Measuring is now complete
    REQUIRE(rig.runner.state() == VerifyState::Settling);
    REQUIRE(rig.runner.report().has_value());
    CHECK(rig.engine.renderedSamples() == measuringAt + dwellSamples());

    // What was submitted is the POST room: it sits on its own prediction. The
    // pre-excitation room would have missed the prediction by the whole
    // correction (a bump of 8 dB against a corridor of 3).
    const auto& report = *rig.runner.report();
    CHECK(report.flaggedCount == 0);
    for (const auto& bin : report.bins) CHECK(std::abs(bin.deltaDb) < 1e-4);
}

TEST_CASE("VERIFY: the pre-excitation snapshot, submitted, would report the unchanged room as a failure") {
    // The fixture's own control: proves the two snapshots differ enough for
    // the test above to tell them apart, so its "flaggedCount == 0" is not a
    // property of a fixture that cannot flag.
    Rig rig;
    rig.serveAlwaysPre = true;  // the FIFO never turns over
    REQUIRE(rig.runner.press());
    rig.pump(1);
    REQUIRE(rig.runner.state() == VerifyState::Measuring);
    rig.pump(300);  // long past the dwell in rendered samples: it submits what it is served
    REQUIRE(rig.runner.report().has_value());
    CHECK(rig.runner.report()->flaggedCount > 0);
}

TEST_CASE("VERIFY: Done reads exactly renderVerifySummary, and the after-measurement lands in group EQ") {
    Rig rig;
    REQUIRE(rig.runToDone());
    REQUIRE(rig.runner.report().has_value());
    CHECK(rig.runner.outcome() == VerifyOutcome::Done);
    CHECK_FALSE(rig.runner.busy());
    CHECK(rig.model.status() == rta::measure::renderVerifySummary(*rig.runner.report()));
    CHECK(rig.model.status().find("0 flagged") != std::string::npos);

    const auto* entry = rig.library.entry(rig.runner.afterTraceId());
    REQUIRE(entry != nullptr);
    CHECK(entry->group == "EQ");
    CHECK(entry->name == "VERIFY @ 12:34:56");
    CHECK(rig.freezes == 1);
    // The trace stored is the snapshot that was compared, not another one.
    const auto* trace = rig.library.trace(rig.runner.afterTraceId());
    REQUIRE(trace != nullptr);
    const auto stored = trace->field(rta::trace::Field::Magnitude);
    REQUIRE(stored.size() == rig.post->transfer->magnitudeDb.size());
    for (std::size_t k = 0; k < stored.size(); ++k) CHECK(stored[k] == rig.post->transfer->magnitudeDb[k]);
}

TEST_CASE("VERIFY waits for the coherence gate: a post snapshot without coherence is never submitted") {
    Rig rig;
    const auto withCoherence = rig.post;
    rig.post = verifyfixture::makeSnapshot(withCoherence->transfer->magnitudeDb, {});  // gate not open yet
    REQUIRE(rig.runner.press());
    rig.pump(1);
    REQUIRE(rig.runner.state() == VerifyState::Measuring);
    rig.pump(200);  // 200 blocks: far past the dwell
    CHECK(rig.runner.state() == VerifyState::Measuring);
    CHECK_FALSE(rig.runner.report().has_value());

    rig.post = withCoherence;  // the gate opens
    rig.pump(1);
    CHECK(rig.runner.state() == VerifyState::Settling);
    CHECK(rig.runner.report().has_value());
}

TEST_CASE("VERIFY waiting longer than 5000 ms disarms and says so") {
    Rig rig;
    REQUIRE(rig.runner.press());
    // The device never calls back: nothing renders, the excitation never
    // reaches the settle. 5000 ms is still allowed; 5000.x is not
    // (captureTimedOut compares with >).
    rig.nowMs += 5000.0;
    rig.runner.poll();
    CHECK(rig.runner.state() == VerifyState::Waiting);
    CHECK(rig.runner.busy());

    rig.nowMs += 1.0;
    rig.runner.poll();
    CHECK(rig.runner.state() == VerifyState::Idle);
    CHECK_FALSE(rig.runner.busy());
    CHECK(rig.runner.outcome() == VerifyOutcome::TimedOut);
    CHECK(rig.model.status().find("timed out") != std::string::npos);
    CHECK(rig.model.status().find("5000 ms") != std::string::npos);
    // It DISARMED: rendering the ramp out brings the engine to quiescence, so
    // nothing is left playing (and the next press is not refused).
    rig.renderOnly(8);
    CHECK(rig.engine.sourceIsQuiescent());
    CHECK(rig.runner.press());
}

TEST_CASE("VERIFY: a stalled dwell (no more rendering, no usable snapshot) also times out and disarms") {
    Rig rig;
    REQUIRE(rig.runner.press());
    rig.pump(1);
    REQUIRE(rig.runner.state() == VerifyState::Measuring);
    const double dwellMs = dwellSeconds() * 1000.0;

    // The allowance is dwell + 5000 ms from Measuring. Either side of it by a
    // millisecond (dwellMs is not exactly representable, so the boundary
    // itself is not asserted).
    const double measuringAt = rig.nowMs;
    rig.nowMs = measuringAt + dwellMs + rta::measure::kEqVerifyTimeoutMs - 1.0;
    rig.runner.poll();
    CHECK(rig.runner.state() == VerifyState::Measuring);

    rig.nowMs = measuringAt + dwellMs + rta::measure::kEqVerifyTimeoutMs + 1.0;
    rig.runner.poll();
    CHECK(rig.runner.state() == VerifyState::Idle);
    CHECK(rig.runner.outcome() == VerifyOutcome::TimedOut);
    rig.renderOnly(8);
    CHECK(rig.engine.sourceIsQuiescent());
}

TEST_CASE("VERIFY aborts, disarmed, when the device stops mid-run") {
    Rig rig;
    REQUIRE(rig.runner.press());
    rig.pump(2);
    REQUIRE(rig.runner.busy());
    rig.env.deviceRunning = false;
    rig.runner.poll();
    CHECK_FALSE(rig.runner.busy());
    CHECK(rig.runner.outcome() == VerifyOutcome::DeviceStopped);
    CHECK(rig.model.status().find("device stopped") != std::string::npos);
    rig.renderOnly(8);
    CHECK(rig.engine.sourceIsQuiescent());
}

TEST_CASE("VERIFY aborts, disarmed, when the grid changes under it or the operator edits the inputs") {
    SECTION("the live fftSize changes") {
        Rig rig;
        REQUIRE(rig.runner.press());
        rig.pump(1);
        rig.post = verifyfixture::makeSnapshot(std::vector<float>(1025, 0.0f), rig.coherence, 2048);
        rig.pump(200);
        CHECK(rig.runner.outcome() == VerifyOutcome::SettingsChanged);
        CHECK_FALSE(rig.runner.busy());
        rig.renderOnly(8);
        CHECK(rig.engine.sourceIsQuiescent());
    }
    SECTION("the transfer averaging is switched to Exponential") {
        Rig rig;
        REQUIRE(rig.runner.press());
        rig.pump(1);
        rig.config.transferAveraging = rta::dsp::TransferAveraging::Exponential;
        rig.pump(200);
        CHECK(rig.runner.outcome() == VerifyOutcome::SettingsChanged);
    }
    SECTION("the filter list is edited") {
        Rig rig;
        REQUIRE(rig.runner.press());
        rig.pump(1);
        rig.model.clearFilters();
        rig.pump(200);
        CHECK(rig.runner.outcome() == VerifyOutcome::InputsChanged);
        CHECK_FALSE(rig.runner.report().has_value());
        rig.renderOnly(8);
        CHECK(rig.engine.sourceIsQuiescent());
    }
}

TEST_CASE("VERIFY builds a FRESH EqVerify per press: a second press after Done is not refused") {
    Rig rig;
    REQUIRE(rig.runToDone());
    const std::string first = rig.runner.afterTraceId();
    REQUIRE_FALSE(first.empty());

    // EqVerify::arm refuses unless Idle (EqVerify.cpp), so a reused instance
    // would refuse here with AlreadyRunning.
    REQUIRE(rig.runner.press());
    CHECK(rig.runner.state() == VerifyState::Waiting);
    CHECK(rig.runner.lastRefusal() == VerifyBlock::None);
    CHECK(rig.runner.outcome() == VerifyOutcome::None);
    CHECK_FALSE(rig.runner.canAdopt());  // the earlier result is superseded, not adoptable mid-run
    REQUIRE(rig.pumpUntil(VerifyState::Done));
    CHECK(rig.runner.afterTraceId() != first);
}

TEST_CASE("ADOPT is enabled only at Done") {
    Rig rig;
    CHECK_FALSE(rig.runner.canAdopt());  // Idle
    REQUIRE(rig.runner.press());
    CHECK_FALSE(rig.runner.canAdopt());  // Waiting
    rig.pump(1);
    CHECK_FALSE(rig.runner.canAdopt());  // Measuring
    REQUIRE(rig.pumpUntil(VerifyState::Settling));
    CHECK_FALSE(rig.runner.canAdopt());  // Settling: the report exists, the excitation is still ramping out
    REQUIRE(rig.pumpUntil(VerifyState::Done));
    CHECK(rig.runner.canAdopt());

    // An early press changes nothing.
    Rig early;
    REQUIRE(early.runner.press());
    const std::size_t unapplied = early.model.unappliedCount();
    CHECK_FALSE(early.runner.adopt());
    CHECK(early.model.unappliedCount() == unapplied);
    CHECK(early.model.measurement().id == "bump");
}
