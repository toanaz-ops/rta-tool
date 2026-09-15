// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-EQ task F (docs/plans/2026-09-07-L7-eq-impl-plan.md; decision record
// docs/dsp/2026-09-06-l7-auto-eq.md sec.8, and the excitation contract of
// docs/dsp/2026-09-06-l7-output-path.md sec.6 verbatim). VERIFY rides the
// output path as a consumer: this file drives a REAL
// rta::platform::OutputEngine with no audio hardware, exactly as
// test_delay_locator.cpp and test_output_policy.cpp do.

#include "measure/EqVerify.h"

#include "rta/gen/Oscillator.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using rta::measure::compareToPrediction;
using rta::measure::EqVerify;
using rta::measure::h1SigmaDb;
using rta::measure::VerifyState;
using rta::measure::VerifyTolerance;
using rta::platform::OutputEngine;
using rta::platform::OutputRole;

namespace {

constexpr double kFs = 48000.0;

std::vector<float*> channelPointers(std::vector<std::vector<float>>& buffers) {
    std::vector<float*> ptrs;
    ptrs.reserve(buffers.size());
    for (auto& channel : buffers) ptrs.push_back(channel.data());
    return ptrs;
}

/// Runs the engine's callback for `blocks` blocks of 512 and reports whether
/// anything non-zero reached `channel`. No device anywhere in this path.
bool pumpAndListen(OutputEngine& engine, int channels, int channel, int blocks) {
    bool heard = false;
    for (int b = 0; b < blocks; ++b) {
        std::vector<std::vector<float>> buffers(static_cast<std::size_t>(channels),
                                                std::vector<float>(512, 0.0f));
        auto ptrs = channelPointers(buffers);
        engine.render(ptrs.data(), channels, 512);
        for (float s : buffers[static_cast<std::size_t>(channel)]) {
            if (s != 0.0f) heard = true;
        }
    }
    return heard;
}

struct Bins {
    std::vector<float> measuredAfterDb;
    std::vector<double> predictedDb;
    std::vector<float> targetDb;
    std::vector<float> measuredBeforeDb;
    std::vector<float> coherence;
    std::vector<std::uint8_t> trusted;
};

/// Every bin sits exactly `deltaDb` off its prediction, so only the two
/// tolerance terms decide whether it is flagged.
Bins makeBins(double deltaDb, float coherenceValue) {
    Bins b;
    const std::size_t m = 8;
    b.predictedDb.assign(m, 1.0);
    b.measuredAfterDb.assign(m, static_cast<float>(1.0 + deltaDb));
    b.targetDb.assign(m, 0.0f);
    b.measuredBeforeDb.assign(m, 4.0f);
    b.coherence.assign(m, coherenceValue);
    b.trusted.assign(m, static_cast<std::uint8_t>(1));
    return b;
}

rta::measure::VerifyReport run(const Bins& b, double corridorHalfWidthDb,
                               double effectiveAverages) {
    VerifyTolerance tolerance;
    tolerance.corridorHalfWidthDb = corridorHalfWidthDb;
    return compareToPrediction(b.measuredAfterDb, b.predictedDb, b.targetDb, b.measuredBeforeDb,
                               b.coherence, b.trusted, effectiveAverages, tolerance);
}

}  // namespace

// --- F1 ---------------------------------------------------------------------
TEST_CASE("EqVerify: the excitation rides the output-path contract, no device") {
    OutputEngine engine;
    engine.prepare(kFs, 4);

    EqVerify::Config config;
    config.outputChannel = 2;
    config.sampleRate = kFs;
    EqVerify verify(engine, config);
    REQUIRE(verify.state() == VerifyState::Idle);

    verify.arm();
    CHECK(verify.state() == VerifyState::Waiting);

    // routeOutput(ch, true) landed as STRICT solo: a verify IS a measurement
    // action, so it takes the same owner ruling a sequence does (L7-OUT
    // record sec.13.2).
    CHECK(engine.role(2) == OutputRole::Routed);
    CHECK(engine.role(0) == OutputRole::None);
    CHECK(engine.role(1) == OutputRole::None);
    CHECK(engine.role(3) == OutputRole::None);

    // Audible on the routed channel is the ORDER proof that matters:
    // OutputEngine::setSource refuses unless the engine is quiescent, so a
    // slot holding a source that renders can only have been filled BEFORE
    // armSource() -- sec.6's order, observed rather than asserted.
    CHECK(pumpAndListen(engine, 4, 2, 4));
    CHECK(engine.renderedSamples() > 0);

    verify.poll();
    CHECK(verify.state() == VerifyState::Measuring);

    const Bins b = makeBins(0.2, 0.99f);
    verify.submitSnapshot(b.measuredAfterDb, b.predictedDb, b.targetDb, b.measuredBeforeDb,
                          b.coherence, b.trusted, 100.0);
    REQUIRE(verify.report().has_value());

    // disarmSource() ran with the snapshot, and the master gate reaches Idle
    // once the callback has rendered the ramp out.
    CHECK(verify.state() == VerifyState::Settling);
    pumpAndListen(engine, 4, 2, 8);
    verify.poll();
    CHECK(engine.sourceIsQuiescent());
    CHECK(verify.state() == VerifyState::Done);
}

TEST_CASE("EqVerify: refuses to arm when the engine is not quiescent") {
    // setSource returns false unless the engine is quiescent. Ignoring that
    // bool would leave whatever source is already loaded in the slot -- a
    // running Locate, say -- and then solo it and arm it, so VERIFY would
    // report over the wrong excitation at the wrong level and never say so.
    OutputEngine engine;
    engine.prepare(kFs, 4);

    // Somebody else owns the engine: a source is loaded and armed.
    REQUIRE(engine.setSource(rta::platform::SourceVariant(
            rta::gen::Oscillator(kFs, 1000.0, -6.0))));
    engine.armSource();
    REQUIRE_FALSE(engine.sourceIsQuiescent());

    EqVerify::Config config;
    config.outputChannel = 2;
    config.sampleRate = kFs;
    EqVerify verify(engine, config);
    verify.arm();

    CHECK(verify.state() == VerifyState::Idle);
    CHECK(verify.lastRefusal() == rta::measure::VerifyRefusal::EngineNotQuiescent);
    // And it did NOT touch the routing on its way out.
    CHECK(engine.role(2) == OutputRole::None);
}

TEST_CASE("EqVerify: arming twice names its own refusal instead of a stale one") {
    // The other early return. Leaving lastRefusal() reading whatever the
    // PREVIOUS attempt set would make a second arm() report a reason it did
    // not have -- an absent result wearing an old result's clothes.
    OutputEngine engine;
    engine.prepare(kFs, 4);

    EqVerify::Config config;
    config.sampleRate = kFs;
    EqVerify verify(engine, config);

    verify.arm();
    REQUIRE(verify.state() == VerifyState::Waiting);
    REQUIRE(verify.lastRefusal() == rta::measure::VerifyRefusal::None);

    verify.arm();  // already running
    CHECK(verify.state() == VerifyState::Waiting);
    CHECK(verify.lastRefusal() == rta::measure::VerifyRefusal::AlreadyRunning);
}

TEST_CASE("EqVerify: renderVerifySummary tells the two absent cases apart") {
    // Two different absences, and the line must not describe one as the other
    // -- that is the round-1 failure shape (an absent result wearing another
    // result's clothes) reappearing in the sentence a human reads.

    // (a) No evidence at all: every bin under the coherence floor.
    rta::measure::VerifyReport blind;
    blind.bins.resize(4);
    blind.trustedBins = 0;
    const std::string blindText = rta::measure::renderVerifySummary(blind);
    CHECK(blindText.find("no trusted bins") != std::string::npos);

    // (b) Evidence exists, but the residual is missing. Cannot come out of
    // compareToPrediction today; nothing asserts that it cannot, and the
    // summary must neither dereference an empty optional nor claim the bins
    // were untrusted when four of them were.
    rta::measure::VerifyReport noResidual;
    noResidual.bins.resize(4);
    noResidual.trustedBins = 4;
    const std::string noResidualText = rta::measure::renderVerifySummary(noResidual);
    CHECK(noResidualText.find("residual not computed") != std::string::npos);
    CHECK(noResidualText.find("4") != std::string::npos);
    CHECK(noResidualText.find("no trusted bins") == std::string::npos);

    CHECK(blindText != noResidualText);
}

TEST_CASE("EqVerify: the report carries the residual before and after") {
    // before = 4 dB off a 0 dB target on every bin, after = 1.2 dB off.
    const Bins b = makeBins(0.2, 0.99f);
    const auto report = run(b, 1.0, 100.0);
    REQUIRE(report.trustedBins == b.trusted.size());
    REQUIRE(report.residualRmsBeforeDb.has_value());
    REQUIRE(report.residualRmsAfterDb.has_value());
    CHECK(std::abs(*report.residualRmsBeforeDb - 4.0) <= 1e-6);
    CHECK(std::abs(*report.residualRmsAfterDb - 1.2) <= 1e-5);
}

TEST_CASE("EqVerify: an all-untrusted capture is distinguishable from a perfect one") {
    // The case VERIFY exists to survive: every bin under the coherence floor.
    // There is no evidence, so there is no residual -- and "no residual" must
    // not render as the same 0.0 dB a flawless pass renders as
    // (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
    Bins blind = makeBins(0.2, 0.99f);
    for (auto& t : blind.trusted) t = static_cast<std::uint8_t>(0);
    const auto blindReport = run(blind, 0.1, 100.0);

    // A genuinely perfect verify: every bin trusted, measurement ON the
    // prediction and ON the target, so both residuals are exactly 0 dB.
    Bins perfect = makeBins(0.0, 0.99f);
    perfect.measuredBeforeDb.assign(perfect.trusted.size(), 0.0f);
    perfect.predictedDb.assign(perfect.trusted.size(), 0.0);
    perfect.measuredAfterDb.assign(perfect.trusted.size(), 0.0f);
    const auto perfectReport = run(perfect, 0.1, 100.0);

    // Both report zero flags -- that alone can never tell them apart.
    REQUIRE(blindReport.flaggedCount == 0);
    REQUIRE(perfectReport.flaggedCount == 0);

    CHECK(blindReport.trustedBins == 0);
    CHECK(perfectReport.trustedBins == perfect.trusted.size());
    CHECK_FALSE(blindReport.residualRmsBeforeDb.has_value());
    CHECK_FALSE(blindReport.residualRmsAfterDb.has_value());
    REQUIRE(perfectReport.residualRmsBeforeDb.has_value());
    CHECK(*perfectReport.residualRmsBeforeDb == 0.0);
    CHECK(*perfectReport.residualRmsAfterDb == 0.0);

    // And the text a human reads says which of the two it is looking at.
    const std::string blindText = rta::measure::renderVerifySummary(blindReport);
    const std::string perfectText = rta::measure::renderVerifySummary(perfectReport);
    CHECK(blindText != perfectText);
    CHECK(blindText.find("no trusted bins") != std::string::npos);
    CHECK(perfectText.find("no trusted bins") == std::string::npos);
}

// --- F2 ---------------------------------------------------------------------
TEST_CASE("EqVerify: a disagreement needs BOTH the corridor and the 3-sigma term") {
    // gamma^2 = 0.99, n_d = 100  ->  3*sigma = 0.185 dB, UNDER the 0.2 dB
    // offset; gamma^2 = 0.90, n_d = 100  ->  3*sigma = 0.614 dB, OVER it.
    // Both are read off h1SigmaDb's own closed form below, not off a run.
    const Bins clean = makeBins(0.2, 0.99f);
    const Bins noisy = makeBins(0.2, 0.90f);

    // Wide corridor, small sigma: the operator does not care, so nothing is
    // flagged even though the offset is many sigma.
    CHECK(run(clean, 1.0, 100.0).flaggedCount == 0);

    // Tight corridor, small sigma: both terms pass -- this is the honest flag.
    CHECK(run(clean, 0.1, 100.0).flaggedCount == clean.predictedDb.size());

    // Tight corridor, large sigma: the operator cares, but the estimate
    // cannot tell this apart from its own noise. Not flagged (record sec.8).
    CHECK(run(noisy, 0.1, 100.0).flaggedCount == 0);
}

TEST_CASE("EqVerify: an untrusted bin is never flagged") {
    Bins b = makeBins(5.0, 0.99f);
    b.trusted[3] = 0;
    const auto report = run(b, 0.1, 100.0);
    REQUIRE(report.bins.size() == b.trusted.size());
    CHECK_FALSE(report.bins[3].flagged);
    CHECK(report.bins[2].flagged);
}

// --- F3 ---------------------------------------------------------------------
TEST_CASE("EqVerify: sigma is Bendat & Piersol's H1 standard deviation in dB") {
    // sigma = (20/ln10) * sqrt((1 - g)/(2 * n_d * g)). Every case below is an
    // identity of that expression, not a value the implementation printed.
    const double dbPerNeper = 20.0 / std::log(10.0);

    // g == 1: a perfectly coherent estimate has zero variance, exactly.
    CHECK(h1SigmaDb(1.0, 64.0) == 0.0);

    // g == 0.5 collapses (1-g)/g to 1, leaving sigma = (20/ln10)/sqrt(2*n_d).
    CHECK(std::abs(h1SigmaDb(0.5, 50.0) - dbPerNeper / std::sqrt(100.0)) <= 1e-12);

    // 1/sqrt(n_d): quadrupling the averages halves sigma, exactly.
    const double sigmaN = h1SigmaDb(0.8, 25.0);
    CHECK(std::abs(h1SigmaDb(0.8, 100.0) - sigmaN / 2.0) <= 1e-12);

    // Monotone decreasing in coherence -- the reason a coherence floor is a
    // data-quality gate at all (EqTrustMask.h).
    CHECK(h1SigmaDb(0.7, 64.0) > h1SigmaDb(0.9, 64.0));
    CHECK(h1SigmaDb(0.9, 64.0) > h1SigmaDb(0.99, 64.0));

    // Degenerate inputs have no standard deviation to report. sigma is a
    // REFUSAL term, so they must read as "infinitely uncertain" -- never NaN,
    // which compares false against everything and would silently disable the
    // 3-sigma half of the rule.
    CHECK_FALSE(std::isnan(h1SigmaDb(0.0, 64.0)));
    CHECK_FALSE(std::isnan(h1SigmaDb(0.9, 0.0)));
    CHECK(h1SigmaDb(0.0, 64.0) > h1SigmaDb(0.01, 64.0));
    CHECK(h1SigmaDb(0.9, 0.0) > h1SigmaDb(0.9, 1.0));
}
