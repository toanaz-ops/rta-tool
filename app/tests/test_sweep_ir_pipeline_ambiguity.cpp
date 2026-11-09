// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H8 review M-1 / M-4 (docs/reviews/2026-11-09-h8-review-fable.md): the M1
// arrival search's prominence gate and its 12 dB rule. Split from
// test_sweep_ir_pipeline.cpp, which sits at the 400-line cap. JUCE-free.
#include <catch2/catch_test_macros.hpp>

#include "SweepIrFixture.h"
#include "measure/SweepIrPipeline.h"

#include <cmath>
#include <string>

using rta::measure::analyseSweepCapture;
using rta::measure::SweepIrOutcome;
using rta::measure::SweepIrRequest;
using rta::measure::SweepIrStatus;
namespace fx = sweep_fixture;

namespace {

constexpr std::size_t kPreRoll = 12345;

SweepIrRequest shortRequest() {
    SweepIrRequest request;
    request.sweep = rta::measure::shippedSweepConfig(48000.0);
    request.sweep.durationSec = 2.0;
    request.tailSeconds = 0.6;
    return request;
}

}  // namespace


namespace {

// The near copy (37 samples, amplitude 0.5) plus a far echo past the window at
// `echoGainDb` relative to it.
SweepIrOutcome analyseWithFarEcho(double echoGainDb) {
    const auto request = shortRequest();
    const rta::gen::Sweep sweep(request.sweep);
    const std::size_t fftSize = rta::measure::sweepIrWindowSize(48000.0, sweep.validBandLowHz());
    const std::size_t farDelay = fftSize + 3000;
    const auto required = rta::measure::requiredTailSamples(request);
    auto capture = fx::delayCapture(request.sweep, kPreRoll, required + farDelay + 1, 37);
    const auto far = fx::delayed(capture.reference, farDelay);
    const auto echoGain = static_cast<float>(0.5 * std::pow(10.0, echoGainDb / 20.0));
    for (std::size_t i = 0; i < capture.measurement.size(); ++i) {
        capture.measurement[i] = 0.5f * capture.measurement[i] + echoGain * far[i];
    }
    return analyseSweepCapture(capture, request);
}

}  // namespace

TEST_CASE("a 30 dB louder echo past the window is refused as ambiguous, not silently (H8 M-1)",
          "[sweep_ir_pipeline]") {
    // Before the fix the whole-signal rms made the near peak look non-prominent
    // (the loud echo inflated it), so M1 never considered it and the refusal
    // carried the plain geometry message. Windowed rms makes it prominent, and
    // the 12 dB rule then flags the pair as ambiguous.
    const auto outcome = analyseWithFarEcho(30.0);
    CHECK(outcome.status == SweepIrStatus::ArrivalOutOfWindow);
    CHECK(outcome.message.find("ambiguous") != std::string::npos);
}

TEST_CASE("the windowed arrival is adopted within 12 dB of the global peak, refused beyond (H8 M-4)",
          "[sweep_ir_pipeline]") {
    const auto within = analyseWithFarEcho(11.0);
    INFO(within.message);
    REQUIRE(within.status == SweepIrStatus::Ok);
    CHECK(within.delaySamples == 37);

    const auto beyond = analyseWithFarEcho(13.0);
    CHECK(beyond.status == SweepIrStatus::ArrivalOutOfWindow);
    CHECK(beyond.message.find("ambiguous") != std::string::npos);

    // A plain geometry refusal (no windowed candidate at all) keeps its message.
    const auto request = shortRequest();
    const auto required = rta::measure::requiredTailSamples(request);
    const auto capture = fx::delayCapture(request.sweep, kPreRoll, required + 40000, 30000);
    const auto geometry = analyseSweepCapture(capture, request);
    if (geometry.status == SweepIrStatus::ArrivalOutOfWindow) {
        CHECK(geometry.message.find("ambiguous") == std::string::npos);
    }
}
