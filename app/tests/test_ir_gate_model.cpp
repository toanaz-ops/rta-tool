// SPDX-License-Identifier: AGPL-3.0-or-later
// H9 / L4c task A2: IrGateModel, the IR pane's numbers and words with no window.
// The gate maths is proven in core/tests/test_ir_gate.cpp; this file proves what
// the app does WITH it: the readout, the ETC, the stored trace.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "measure/IrGateModel.h"

#include <algorithm>
#include <cmath>
#include <string>

using Catch::Approx;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using rta::measure::IrGateMarkers;
using rta::measure::SweepIr;

namespace {

constexpr double kFs = 48000.0;
constexpr std::size_t kOrigin = 1000;
constexpr long kArrival = 240;  // 5 ms after t = 0

/// A direct delta at origin + arrival, and an echo `echoSamples` later.
SweepIr spikes(double echoGain = 0.0, long echoSamples = 0) {
    SweepIr ir;
    ir.samples.assign(6000, 0.0f);
    ir.originIndex = kOrigin;
    ir.arrivalSamples = kArrival;
    ir.sampleRate = kFs;
    ir.samples[kOrigin + static_cast<std::size_t>(kArrival)] = 1.0f;
    if (echoGain != 0.0) ir.samples[kOrigin + static_cast<std::size_t>(kArrival + echoSamples)] = static_cast<float>(echoGain);
    return ir;
}

}  // namespace

TEST_CASE("The default gate reads out its markers and its lowest valid frequency", "[ir][gate][model]") {
    const auto analysis = rta::measure::analyseIrGate(spikes(), IrGateMarkers{});
    REQUIRE(analysis.ok);
    // 1 / (11 ms - 1.375 ms) = 103.9 Hz, printed rounded UP to whole hertz.
    CHECK(analysis.readout == "GATE -1.0..10.0 ms \xc2\xb7 valid above 104 Hz");
    CHECK(analysis.windowText == "gate -1.0..10.0 ms, valid above 104 Hz");
    CHECK(analysis.fMinHz == Approx(1.0 / (0.011 - 0.5 * 0.00275)).epsilon(1e-9));
    CHECK(analysis.bins.size() == analysis.transformSize / 2 + 1);
    CHECK(analysis.transformSize == 16384);
    // A lone delta gated whole is flat 0 dB.
    CHECK(std::abs(analysis.bins[200]) == Approx(1.0).margin(1e-5));
}

TEST_CASE("A refused gate says so, with the number, and has no spectrum", "[ir][gate][model]") {
    const auto analysis = rta::measure::analyseIrGate(spikes(), IrGateMarkers{-1.0, 500.0});
    CHECK_FALSE(analysis.ok);
    CHECK(analysis.bins.empty());
    CHECK_THAT(analysis.readout, StartsWith("GATE refused -- "));
    CHECK_THAT(analysis.readout, ContainsSubstring("500.0"));
    CHECK_THAT(analysis.refusal, ContainsSubstring("500.0"));
    CHECK_FALSE(rta::measure::gatedTrace(spikes(), analysis, "capture-test", "dev", "roles").has_value());
}

TEST_CASE("The ETC is 0 dB at the arrival and silent where the IR is", "[ir][gate][model]") {
    const auto etc = rta::measure::buildEtc(spikes(0.5, 480));  // echo at +10 ms
    REQUIRE_FALSE(etc.db.empty());
    CHECK(etc.startMs == Approx(-5.0).margin(0.03));
    CHECK(etc.endMs() == Approx(50.0).margin(0.03));
    const auto at = [&](double ms) {
        return static_cast<std::size_t>(std::llround((ms - etc.startMs) / etc.stepMs));
    };
    CHECK(etc.db[at(0.0)] == Approx(0.0).margin(1e-4));
    CHECK(etc.db[at(10.0)] == Approx(20.0 * std::log10(0.5)).margin(1e-3));  // -6.02 dB
    CHECK(etc.db[at(3.0)] <= -100.0f);
    // The peak-hold spreads a spike by half its window either side and no further.
    CHECK(etc.db[at(0.0 + 0.1)] == Approx(0.0).margin(1e-4));
    CHECK(etc.db[at(0.0 + 0.3)] <= -100.0f);
}

TEST_CASE("The stored trace is the gated response, labelled as such", "[ir][gate][model]") {
    const auto ir = spikes();
    const auto analysis = rta::measure::analyseIrGate(ir, IrGateMarkers{});
    const auto trace = rta::measure::gatedTrace(ir, analysis, "capture-test", "Interface X", "ch0=Measurement");
    REQUIRE(trace.has_value());
    const auto& meta = trace->meta();
    CHECK(meta.id == "capture-test");
    CHECK(meta.window == "gate -1.0..10.0 ms, valid above 104 Hz");
    CHECK(meta.fftSize == 16384);
    CHECK(meta.sampleRate == Approx(kFs));
    CHECK(meta.deviceName == "Interface X");
    CHECK(meta.channelRoles == "ch0=Measurement");
    // The arrival has been taken out of the phase, so XOVER must be told.
    CHECK(meta.appliedDelaySamples == kArrival);
    CHECK(trace->pointCount() == 8193);
    CHECK(trace->has(rta::trace::Field::Phase));
    CHECK_FALSE(trace->has(rta::trace::Field::Coherence));  // never a made-up one
    CHECK(trace->field(rta::trace::Field::Magnitude)[300] == Approx(0.0).margin(1e-4));
    CHECK(std::abs(trace->field(rta::trace::Field::Phase)[300]) <= 1e-4);
}

TEST_CASE("Valid-above text rounds up to whole hertz", "[ir][gate][model]") {
    CHECK(rta::measure::validAboveText(103.896) == "valid above 104 Hz");
    CHECK(rta::measure::validAboveText(104.0) == "valid above 104 Hz");
    CHECK(rta::measure::validAboveText(104.2) == "valid above 105 Hz");
}
