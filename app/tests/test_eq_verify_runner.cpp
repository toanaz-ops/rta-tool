// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-EQ UI wave B, task T7 (docs/plans/2026-09-29-eq-ui-lane-plan.md, D14):
// every reason VERIFY refuses to start, and what a refusal leaves untouched.
// Each case breaks EXACTLY ONE precondition of an otherwise-startable rig, so
// a refusal that fires for the wrong reason (or not at all) fails its own
// case. The run itself (dwell, timeout, ADOPT) is test_eq_verify_runner_flow.cpp.
#include "EqVerifyRunnerFixture.h"

#include <catch2/catch_test_macros.hpp>

#include "rta/gen/Noise.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

using rta::measure::VerifyBlock;
using rta::measure::VerifyState;
using verifyfixture::Rig;

namespace {

/// The refusal contract every case shares: press() said no, for THIS reason,
/// the operator can read it, and the output engine was not touched.
void requireRefused(Rig& rig, VerifyBlock kind, const std::string& textPart) {
    const auto blocker = rig.runner.blocker();
    CHECK(blocker.kind == kind);
    CHECK(blocker.text.find(textPart) != std::string::npos);

    CHECK_FALSE(rig.runner.press());
    CHECK(rig.runner.lastRefusalForTest() == kind);
    CHECK(rig.model.status().find("VERIFY refused") != std::string::npos);
    CHECK(rig.model.status().find(textPart) != std::string::npos);
    CHECK(rig.runner.stateForTest() == VerifyState::Idle);
    CHECK_FALSE(rig.runner.busy());
    // Nothing armed, nothing routed: the refusal came before the engine.
    CHECK(rig.engine.role(0) == rta::platform::OutputRole::None);
    CHECK(rig.engine.sourceIsQuiescent());
}

}  // namespace

TEST_CASE("VERIFY: a fully-set-up rig is startable, and the excitation is D14's") {
    Rig rig;
    REQUIRE(rig.model.unappliedCount() > 0);
    CHECK(rig.runner.blocker().kind == VerifyBlock::None);
    REQUIRE(rig.runner.press());
    CHECK(rig.runner.stateForTest() == VerifyState::Waiting);
    CHECK(rig.runner.busy());
    // D14: output 0, and the level/corridor/sigma constants the plan names.
    CHECK(rig.engine.role(0) == rta::platform::OutputRole::Routed);
    CHECK(rig.engine.role(1) == rta::platform::OutputRole::None);
    CHECK(rta::measure::kEqVerifyOutputChannel == 0);
    CHECK(rta::measure::kEqVerifyLevelDbFs == -12.0);
    CHECK(rta::measure::kEqVerifyCorridorDb == 3.0);
    CHECK(rta::measure::kEqVerifySigmaMultiple == 3.0);

    // What actually reaches the speaker: pink noise at -12 dBFS RMS on output
    // 0 and exact silence on output 1. The first 4096 samples (gate ramp-in)
    // are skipped. 1 dB is the tolerance a 4 s RMS estimate of pink noise can
    // honestly claim; the seed is fixed, so the run is deterministic.
    double sumSquares = 0.0;
    std::size_t counted = 0;
    float peakOther = 0.0f;
    for (int b = 0; b < 400; ++b) {
        std::vector<std::vector<float>> buffers(verifyfixture::kChannels, std::vector<float>(verifyfixture::kBlock, 0.0f));
        std::vector<float*> ptrs{ buffers[0].data(), buffers[1].data() };
        rig.engine.render(ptrs.data(), verifyfixture::kChannels, verifyfixture::kBlock);
        if (b < 8) continue;
        for (const float s : buffers[0]) { sumSquares += static_cast<double>(s) * s; ++counted; }
        for (const float s : buffers[1]) peakOther = std::max(peakOther, std::abs(s));
    }
    const double rmsDb = 10.0 * std::log10(sumSquares / static_cast<double>(counted));
    INFO("measured RMS " << rmsDb << " dBFS");
    CHECK(std::abs(rmsDb - rta::measure::kEqVerifyLevelDbFs) < 1.0);
    CHECK(peakOther == 0.0f);
}

TEST_CASE("VERIFY refuses with no measurement picked") {
    Rig rig(/*pick*/ false, /*autoEq*/ false);
    requireRefused(rig, VerifyBlock::NoMeasurement, "pick one first");
}

TEST_CASE("VERIFY refuses when no filter is unapplied: none committed, then all applied") {
    Rig rig(/*pick*/ true, /*autoEq*/ false);
    REQUIRE(rig.model.session().committed().empty());
    requireRefused(rig, VerifyBlock::NoUnappliedFilter, "not yet marked applied");

    // The other way to have nothing to check: every filter already marked.
    Rig marked;
    for (std::size_t i = 0; i < marked.model.session().committed().size(); ++i) marked.model.setApplied(i, true);
    REQUIRE(marked.model.unappliedCount() == 0);
    requireRefused(marked, VerifyBlock::NoUnappliedFilter, "not yet marked applied");
}

TEST_CASE("VERIFY refuses in SYNTHETIC mode by name, not as a generic device error (D14)") {
    Rig rig;
    rig.env.synthetic = true;
    rig.env.deviceRunning = false;  // SYNTHETIC stops the device
    requireRefused(rig, VerifyBlock::Synthetic, "SYNTHETIC");
    CHECK(rig.runner.blocker().text.find("LIVE") != std::string::npos);  // and says the way out
}

TEST_CASE("VERIFY refuses when the audio device is not running") {
    Rig rig;
    rig.env.deviceRunning = false;
    requireRefused(rig, VerifyBlock::DeviceNotRunning, "not running");
}

TEST_CASE("VERIFY refuses while LOCATE or CAL owns the output or the capture") {
    Rig rig;
    rig.env.captureBusy = true;
    requireRefused(rig, VerifyBlock::CaptureBusy, "LOCATE or CAL");
}

TEST_CASE("VERIFY refuses with no TRANSFER measurement published: no snapshot, and no transfer block") {
    Rig none;
    none.nullSnapshot = true;
    requireRefused(none, VerifyBlock::NoTransfer, "feed a reference");

    Rig noTransfer;
    noTransfer.pre = std::make_shared<rta::measure::Snapshot>();  // an RTA-only snapshot: no `transfer`
    requireRefused(noTransfer, VerifyBlock::NoTransfer, "feed a reference");
}

TEST_CASE("VERIFY refuses when fftSize or sample rate differ from the bound trace, naming both") {
    Rig fft;
    // Only the fftSize field differs (the magnitude length and rate match), so
    // this case cannot be satisfied by the length check below.
    fft.pre = verifyfixture::makeSnapshot(std::vector<float>(verifyfixture::kBins, 0.0f), fft.coherence, 2048);
    requireRefused(fft, VerifyBlock::GridMismatch, "live fftSize 2048");
    CHECK(fft.runner.blocker().text.find("bound trace 4096") != std::string::npos);

    Rig fs;
    fs.pre = verifyfixture::makeSnapshot(std::vector<float>(verifyfixture::kBins, 0.0f), fs.coherence,
                                         verifyfixture::kFft, 44100.0);
    requireRefused(fs, VerifyBlock::GridMismatch, "44100 Hz");

    // fftSize and rate agree, the published spectrum is the wrong length.
    Rig length;
    length.pre = verifyfixture::makeSnapshot(std::vector<float>(1025, 0.0f), length.coherence);
    requireRefused(length, VerifyBlock::GridMismatch, "live fftSize 4096");
}

TEST_CASE("VERIFY refuses under Exponential transfer averaging") {
    Rig rig;
    rig.config.transferAveraging = rta::dsp::TransferAveraging::Exponential;
    requireRefused(rig, VerifyBlock::ExponentialAveraging, "Exponential");
}

TEST_CASE("VERIFY refuses when the engine is not quiescent, and leaves the other owner's routing alone") {
    Rig rig;
    // Somebody else's excitation is in the slot and armed (a running Locate,
    // say): setSource refuses, and ignoring that would solo and re-arm THEIR
    // source at VERIFY's level.
    REQUIRE(rig.engine.setSource(rta::platform::SourceVariant(
        rta::gen::PinkNoise(rta::gen::Pcg32(5, 1), -30.0))));
    REQUIRE(rig.engine.routeOutput(1, true));
    rig.engine.armSource();

    CHECK(rig.runner.blocker().kind == VerifyBlock::None);  // only arm() can see this one
    CHECK_FALSE(rig.runner.press());
    CHECK(rig.runner.lastRefusalForTest() == VerifyBlock::EngineNotQuiescent);
    CHECK(rig.model.status().find("not quiescent") != std::string::npos);
    CHECK(rig.runner.stateForTest() == VerifyState::Idle);
    CHECK_FALSE(rig.runner.busy());
    CHECK(rig.engine.role(1) == rta::platform::OutputRole::Routed);  // theirs, untouched
    CHECK(rig.engine.role(0) == rta::platform::OutputRole::None);    // VERIFY's, never taken
}

TEST_CASE("VERIFY refuses to start a second run while one is in flight") {
    Rig rig;
    REQUIRE(rig.runner.press());
    CHECK(rig.runner.blocker().kind == VerifyBlock::Running);
    CHECK_FALSE(rig.runner.press());
    CHECK(rig.runner.lastRefusalForTest() == VerifyBlock::Running);
    CHECK(rig.runner.stateForTest() == VerifyState::Waiting);  // the first run is undisturbed
}

TEST_CASE("VERIFY without an attached engine says so instead of dereferencing null") {
    rta::trace::TraceLibrary library;
    rta::measure::EqPaneModel model;
    rta::measure::EqVerifyRunner runner(model);
    CHECK(runner.blocker().kind == VerifyBlock::NotAttached);
    CHECK_FALSE(runner.press());
    CHECK_FALSE(runner.busy());
    CHECK_FALSE(runner.canAdopt());
}

TEST_CASE("every VerifyBlock reads as a distinct, non-empty sentence except None") {
    const VerifyBlock kinds[] = { VerifyBlock::NotAttached,       VerifyBlock::NoMeasurement,
                                  VerifyBlock::NoUnappliedFilter, VerifyBlock::Running,
                                  VerifyBlock::Synthetic,         VerifyBlock::DeviceNotRunning,
                                  VerifyBlock::CaptureBusy,       VerifyBlock::NoTransfer,
                                  VerifyBlock::GridMismatch,      VerifyBlock::ExponentialAveraging,
                                  VerifyBlock::EngineNotQuiescent };
    CHECK(rta::measure::verifyBlockText(VerifyBlock::None).empty());
    for (std::size_t i = 0; i < std::size(kinds); ++i) {
        CHECK_FALSE(rta::measure::verifyBlockText(kinds[i]).empty());
        for (std::size_t j = i + 1; j < std::size(kinds); ++j) {
            CHECK(rta::measure::verifyBlockText(kinds[i]) != rta::measure::verifyBlockText(kinds[j]));
        }
    }
}
