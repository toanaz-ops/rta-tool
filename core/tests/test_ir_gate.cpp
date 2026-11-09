// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane H9 / L4c: the time gate on an impulse response. Every expectation is a
// closed form over a two-spike IR (a direct delta and one echo), never a number
// a previous run printed. Record: docs/dsp/2026-11-09-l4c-ir-gate.md sec. 5.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "rta/ir/IrGate.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

using Catch::Approx;
using Catch::Matchers::ContainsSubstring;
using rta::ir::GateRefusal;
using rta::ir::GateSpec;

namespace {

constexpr double kFs = 48000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr long kArrival = 240;              // 5 ms after t = 0
constexpr std::size_t kLength = 2400;       // 50 ms of IR

/// A direct delta at the arrival plus an echo `echoSamples` later (0 = none).
rta::ir::Deconvolution twoSpikes(double echoGain, long echoSamples) {
    rta::ir::Deconvolution ir;
    ir.samples.assign(kLength, 0.0f);
    ir.originIndex = 0;
    ir.sampleRate = kFs;
    ir.samples[static_cast<std::size_t>(kArrival)] = 1.0f;
    if (echoGain != 0.0) ir.samples[static_cast<std::size_t>(kArrival + echoSamples)] = static_cast<float>(echoGain);
    return ir;
}

GateSpec defaultSpec() {
    GateSpec spec;
    spec.arrivalSamples = kArrival;
    return spec;
}

/// Float-FFT tolerance, derived rather than chosen (memory/float32-fft-precision.md).
/// `RealFft` is single precision, so the error floor is set by the LARGEST bin,
/// which for a spike IR is the L1 norm of the gated taps (<= 1 + a), not by the
/// bin under test. A radix-2 butterfly chain of log2(T) stages adds rounding of
/// order eps per stage per unit of input magnitude; four eps per stage covers
/// the twiddle error too. The lead-in rotation is one more complex multiply by a
/// float-rounded cos/sin (about 2 eps each, so 4 eps after the product).
double floatTolerance(std::size_t transformSize, double l1Norm) {
    constexpr double eps = std::numeric_limits<float>::epsilon() / 2.0;  // 2^-24
    return 4.0 * eps * std::log2(static_cast<double>(transformSize)) * l1Norm + 4.0 * eps;
}

}  // namespace

TEST_CASE("A gate that ends before the echo leaves the response flat", "[ir][gate]") {
    // Echo at +12.5 ms, gate ends at +10 ms (S=-48, E=480 at 48 kHz).
    const auto ir = twoSpikes(0.5, 600);
    const auto result = rta::ir::gateSpectrum(ir, defaultSpec());
    REQUIRE(result.ok());
    REQUIRE(result.spectrum.bins.size() == defaultSpec().transformSize / 2 + 1);

    const double tol = floatTolerance(defaultSpec().transformSize, 1.0);
    double worst = 0.0;
    for (const auto& bin : result.spectrum.bins) worst = std::max(worst, std::abs(std::abs(bin) - 1.0));
    CAPTURE(worst, tol);
    CHECK(worst <= tol);
}

TEST_CASE("An echo inside the closing ramp is weighted by the half-Hann", "[ir][gate]") {
    // Gate: S=-48, E=480, N=528, Fo=132 so the ramp starts at n=396 = +348
    // samples after the arrival. Echo at +420 samples is n=468, m=72 into a
    // 132-sample ramp. Expected weight, written as cos^2 (an independent algebraic
    // form of 0.5*(1+cos)): w = cos^2(pi*72/(2*132)).
    constexpr long kEcho = 420;
    constexpr double kGain = 0.5;
    const double w = std::pow(std::cos(kPi * 72.0 / (2.0 * 132.0)), 2.0);
    REQUIRE(w > 0.3);  // far above the float floor: a ripple of a*w ~ 0.21

    const auto ir = twoSpikes(kGain, kEcho);
    const auto result = rta::ir::gateSpectrum(ir, defaultSpec());
    REQUIRE(result.ok());
    REQUIRE(result.fadeOutSamples == 132);

    const std::size_t transform = defaultSpec().transformSize;
    const double tol = floatTolerance(transform, 1.0 + kGain * w);
    double worst = 0.0;
    for (std::size_t k = 0; k < result.spectrum.bins.size(); ++k) {
        const double theta = -2.0 * kPi * static_cast<double>(k) * static_cast<double>(kEcho) / static_cast<double>(transform);
        const double expected = std::abs(1.0 + kGain * w * std::polar(1.0, theta));
        worst = std::max(worst, std::abs(std::abs(result.spectrum.bins[k]) - expected));
    }
    CAPTURE(worst, tol);
    CHECK(worst <= tol);

    // The fade-in half-Hann, same form: a delta 12 samples into the 24-sample opening ramp.
    // Gate the original IR so that the DIRECT delta sits at n=12 of the gate.
    GateSpec onRamp = defaultSpec();
    onRamp.startMs = -12.0 / 48.0;  // S = -12: the delta is sample n=12 of the gate
    const auto ramp = rta::ir::gateSpectrum(twoSpikes(0.0, 0), onRamp);
    REQUIRE(ramp.ok());
    REQUIRE(ramp.fadeInSamples == 24);
    const double wIn = std::pow(std::sin(kPi * 12.0 / (2.0 * 24.0)), 2.0);  // sin^2, vs the code's 0.5*(1-cos)
    CHECK(std::abs(ramp.spectrum.bins[0]) == Approx(wIn).margin(floatTolerance(transform, 1.0)));
    CHECK(std::abs(ramp.spectrum.bins[100]) == Approx(wIn).margin(floatTolerance(transform, 1.0)));
}

TEST_CASE("The gated phase is referenced to the arrival", "[ir][gate]") {
    const std::size_t transform = defaultSpec().transformSize;

    // Direct sound only: phase 0 at every bin.
    const auto direct = rta::ir::gateSpectrum(twoSpikes(0.0, 0), defaultSpec());
    REQUIRE(direct.ok());
    CHECK(direct.spectrum.leadInSamples == 48);  // -S: the routine's own lead-in, not a second copy
    const double tol = floatTolerance(transform, 1.0);
    double worstPhase = 0.0;
    for (const auto& bin : direct.spectrum.bins) worstPhase = std::max(worstPhase, static_cast<double>(std::abs(std::arg(bin))));
    CAPTURE(worstPhase, tol);
    CHECK(worstPhase <= tol * 2.0);

    // A delta 100 samples AFTER the arrival: phase -2*pi*k*100/T. If the rotation
    // were skipped the lead-in (48 samples) would show up as an extra +48 here.
    constexpr long kLater = 100;
    GateSpec spec = defaultSpec();
    const auto ir = twoSpikes(1.0, kLater);  // equal spikes would interfere; remove the direct one
    auto only = ir;
    only.samples[static_cast<std::size_t>(kArrival)] = 0.0f;
    const auto shifted = rta::ir::gateSpectrum(only, spec);
    REQUIRE(shifted.ok());
    double worst = 0.0;
    for (std::size_t k = 0; k < shifted.spectrum.bins.size(); k += 7) {
        const double expected = -2.0 * kPi * static_cast<double>(k) * static_cast<double>(kLater) / static_cast<double>(transform);
        worst = std::max(worst, std::abs(std::remainder(std::arg(shifted.spectrum.bins[k]) - expected, 2.0 * kPi)));
    }
    CAPTURE(worst);
    CHECK(worst <= tol * 2.0);
}

TEST_CASE("A gate that starts after the arrival is still referenced to it", "[ir][gate]") {
    // S=+96, E=576: the delta sits 240 samples after the arrival, inside the flat
    // part. There are no pre-arrival samples at all; the lead-in is padded to 1.
    GateSpec spec = defaultSpec();
    spec.startMs = 2.0;
    spec.endMs = 12.0;
    auto ir = twoSpikes(0.0, 0);
    ir.samples[static_cast<std::size_t>(kArrival)] = 0.0f;
    ir.samples[static_cast<std::size_t>(kArrival + 240)] = 1.0f;
    const auto result = rta::ir::gateSpectrum(ir, spec);
    REQUIRE(result.ok());
    CHECK(result.spectrum.leadInSamples == 1);
    const std::size_t transform = spec.transformSize;
    const double tol = floatTolerance(transform, 1.0);
    for (std::size_t k = 0; k < result.spectrum.bins.size(); k += 11) {
        const double expected = -2.0 * kPi * static_cast<double>(k) * 240.0 / static_cast<double>(transform);
        CHECK(std::abs(std::remainder(std::arg(result.spectrum.bins[k]) - expected, 2.0 * kPi)) <= tol * 2.0);
        CHECK(std::abs(std::abs(result.spectrum.bins[k]) - 1.0) <= tol);
    }
}

TEST_CASE("The lowest valid frequency is one over the gate less half its closing ramp", "[ir][gate]") {
    const auto ir = twoSpikes(0.0, 0);
    // Default: 11 ms gate, 25 % = 2.75 ms ramp -> 1 / (11 - 1.375) ms.
    const auto standard = rta::ir::gateSpectrum(ir, defaultSpec());
    REQUIRE(standard.ok());
    CHECK(standard.fMinHz == Approx(1.0 / (0.011 - 0.5 * 0.00275)).epsilon(1e-9));

    GateSpec wide = defaultSpec();
    wide.startMs = 0.0;
    wide.endMs = 20.0;
    wide.fadeOutFraction = 0.5;
    const auto longer = rta::ir::gateSpectrum(ir, wide);
    REQUIRE(longer.ok());
    CHECK(longer.fMinHz == Approx(1.0 / (0.020 - 0.5 * 0.010)).epsilon(1e-9));
}

TEST_CASE("Every refusal carries the value that broke it", "[ir][gate]") {
    const auto ir = twoSpikes(0.0, 0);

    SECTION("start not before end") {
        GateSpec spec = defaultSpec();
        spec.startMs = 8.0;
        spec.endMs = 3.0;
        const auto result = rta::ir::gateSpectrum(ir, spec);
        CHECK(result.refusal == GateRefusal::StartNotBeforeEnd);
        CHECK(result.refusalValue == Approx(-5.0));
        CHECK_THAT(result.message, ContainsSubstring("8.0"));
        CHECK_THAT(result.message, ContainsSubstring("3.0"));
        spec.startMs = spec.endMs = 5.0;
        CHECK(rta::ir::gateSpectrum(ir, spec).refusal == GateRefusal::StartNotBeforeEnd);
    }
    SECTION("gate shorter than its fades") {
        GateSpec spec = defaultSpec();
        spec.startMs = 0.0;
        spec.endMs = 0.6;  // 29 samples against a 24-sample ramp in and a 7-sample ramp out
        const auto result = rta::ir::gateSpectrum(ir, spec);
        CHECK(result.refusal == GateRefusal::GateShorterThanFades);
        CHECK(result.refusalValue == Approx(0.6).margin(0.02));
        CHECK_THAT(result.message, ContainsSubstring("0.6"));
        CHECK_THAT(result.message, ContainsSubstring("0.5 ms in"));
    }
    SECTION("start before the IR") {
        GateSpec spec = defaultSpec();
        spec.startMs = -6.0;  // the arrival is 5.0 ms into the IR
        const auto result = rta::ir::gateSpectrum(ir, spec);
        CHECK(result.refusal == GateRefusal::StartBeforeIr);
        CHECK(result.refusalValue == Approx(-6.0));
        CHECK_THAT(result.message, ContainsSubstring("-6.0"));
        CHECK_THAT(result.message, ContainsSubstring("-5.0"));
    }
    SECTION("end past the IR") {
        GateSpec spec = defaultSpec();
        spec.endMs = 60.0;  // the IR runs 45.0 ms past the arrival
        const auto result = rta::ir::gateSpectrum(ir, spec);
        CHECK(result.refusal == GateRefusal::EndPastIr);
        CHECK(result.refusalValue == Approx(60.0));
        CHECK_THAT(result.message, ContainsSubstring("60.0"));
        CHECK_THAT(result.message, ContainsSubstring("45.0"));
    }
    SECTION("an unusable source") {
        rta::ir::Deconvolution empty;
        empty.sampleRate = kFs;
        CHECK(rta::ir::gateSpectrum(empty, defaultSpec()).refusal == GateRefusal::EmptyIr);
        auto noRate = ir;
        noRate.sampleRate = 0.0;
        CHECK(rta::ir::gateSpectrum(noRate, defaultSpec()).refusal == GateRefusal::BadSampleRate);
    }
}
