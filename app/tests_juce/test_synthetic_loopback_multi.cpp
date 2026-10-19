// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ALIGN-UI lane wave A, task T0 (docs/plans/2026-10-20-align-ui-lane-plan.md,
// decision 10): the SYNTHETIC loopback renders TWO output channels, each with
// its own path into the measurement copy. test_synthetic_loopback.cpp proves
// the one-output behaviour H4 settled; this file proves the second output
// through the REAL thread and a REAL CaptureBus, bit-exact against an
// independent engine rendered by hand:
//   - the reference is the lowest ROUTED output's block;
//   - the measurement is path0(output 0) + path1(output 1), so soloing the
//     sub hears the sub's own path and nothing of the main's;
//   - soloing output 0 is bit-identical to the one-path code, whatever the
//     second path is (an unrouted output is exact zeros, and x + 0 is exact).
//
// The first two blocks of a capture are pink fallback (the engine is not armed
// yet) and the tail end of the stream is the engine's: expectations are built
// stream-wise from the same float operations the thread performs, never from
// a tolerance.

#include <catch2/catch_test_macros.hpp>

#include "measure/OutputPolicy.h"
#include "measure/SyntheticCrossover.h"
#include "measure/SyntheticImpairment.h"
#include "measure/SyntheticInput.h"
#include "rta/gen/Oscillator.h"
#include "rta/platform/CaptureBus.h"
#include "rta/platform/OutputEngine.h"

#include <juce_core/juce_core.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

using rta::measure::SyntheticInput;
using rta::platform::CaptureBus;
using rta::platform::ChannelRole;
using rta::platform::OutputEngine;

namespace {

constexpr double kFs = 48000.0;
constexpr int kBlock = 512;
constexpr int kMainDelay = 4;
constexpr int kSubDelay = 100;
// A tone the two paths treat differently (the LR4 pair is -6 dB each at the
// crossover), so "the wrong path was applied" moves every sample.
constexpr double kToneHz = 100.0;
constexpr double kToneDbFsPeak = -12.0;

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        if (predicate()) return true;
        juce::Thread::sleep(5);
    }
    return predicate();
}

std::vector<float> drainAll(CaptureBus& bus, int channel) {
    auto* ring = bus.ring(channel);
    REQUIRE(ring != nullptr);
    std::vector<float> all(ring->availableToRead());
    REQUIRE(ring->peek(all) == all.size());
    return all;
}

struct Recorded {
    std::vector<float> reference;
    std::vector<float> measurement;
    std::uint64_t rendered = 0;
    std::vector<float> expectedOut[2];  ///< what each output rendered since the arm
};

void routeEngine(OutputEngine& engine, const std::function<void(OutputEngine&)>& route) {
    REQUIRE(engine.setSource(rta::gen::Oscillator(kFs, kToneHz, kToneDbFsPeak)));
    route(engine);
    engine.armSource();
}

/// Runs the real thread against a real bus with the two paths a SYNTHETIC
/// ALIGN run uses (main: LR4 high-pass at 100 Hz after 4 samples, sub: LR4
/// low-pass at 100 Hz after 100), and returns what the bus recorded next to an
/// independent two-channel render of the same source.
Recorded record(const std::function<void(OutputEngine&)>& route) {
    CaptureBus bus(rta::platform::kFixedRingCapacitySamples);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    REQUIRE(bus.config().setRole(1, ChannelRole::Reference));
    OutputEngine engine;

    SyntheticInput::Config config;
    config.measurementDelaySamples = kMainDelay;
    config.measurementFilter = rta::measure::lr4HighPass(100.0, kFs);
    config.secondOutputDelaySamples = kSubDelay;
    config.secondOutputFilter = rta::measure::lr4LowPass(100.0, kFs);
    config.loopback = &engine;
    auto input = std::make_unique<SyntheticInput>(bus, config);

    // The constructor prepare()d the engine; arm only afterwards.
    routeEngine(engine, route);
    REQUIRE(waitUntil([&] { return engine.renderedSamples() >= 8u * kBlock; }, 10000));
    input.reset();  // joins the thread: every rendered block has been pushed

    Recorded out;
    out.rendered = engine.renderedSamples();
    REQUIRE(out.rendered >= 8u * kBlock);
    REQUIRE(out.rendered % kBlock == 0);
    out.reference = drainAll(bus, 1);
    out.measurement = drainAll(bus, 0);
    REQUIRE(out.reference.size() == out.measurement.size());
    REQUIRE(out.reference.size() >= out.rendered);

    OutputEngine expected;
    expected.prepare(kFs, SyntheticInput::kOutputChannels);
    routeEngine(expected, route);
    out.expectedOut[0].resize(out.rendered);
    out.expectedOut[1].resize(out.rendered);
    for (std::size_t at = 0; at < out.rendered; at += kBlock) {
        float* outs[2] = {out.expectedOut[0].data() + at, out.expectedOut[1].data() + at};
        expected.render(outs, 2, kBlock);
    }
    return out;
}

/// path0(out0) + path1(out1) over the whole recorded stream, the same float
/// operations the thread does. Before the arm the thread feeds the pink
/// fallback to path 0 and zeros to path 1.
std::vector<float> expectedMeasurement(const Recorded& r) {
    const std::size_t offset = r.reference.size() - static_cast<std::size_t>(r.rendered);
    std::vector<float> out0(r.reference.size(), 0.0f), out1(r.reference.size(), 0.0f);
    for (std::size_t i = 0; i < r.reference.size(); ++i) {
        if (i < offset) {
            out0[i] = r.reference[i];
        } else {
            out0[i] = r.expectedOut[0][i - offset];
            out1[i] = r.expectedOut[1][i - offset];
        }
    }
    rta::measure::MeasurementPath main(kMainDelay, rta::measure::lr4HighPass(100.0, kFs));
    rta::measure::MeasurementPath sub(kSubDelay, rta::measure::lr4LowPass(100.0, kFs));
    std::vector<float> p0(out0.size()), p1(out1.size()), sum(out0.size());
    main.process(out0, p0);
    sub.process(out1, p1);
    for (std::size_t i = 0; i < sum.size(); ++i) sum[i] = p0[i] + p1[i];
    return sum;
}

void requireReference(const Recorded& r, int referenceOutput) {
    const std::size_t offset = r.reference.size() - static_cast<std::size_t>(r.rendered);
    bool anyNonZero = false;
    for (std::size_t i = 0; i < r.rendered; ++i) {
        INFO("reference sample " << i);
        REQUIRE(r.reference[offset + i] == r.expectedOut[referenceOutput][i]);
        anyNonZero = anyNonZero || r.expectedOut[referenceOutput][i] != 0.0f;
    }
    REQUIRE(anyNonZero);  // not vacuous: the oracle itself is a signal
}

void requireMeasurement(const Recorded& r) {
    const auto expected = expectedMeasurement(r);
    bool anyNonZero = false;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        INFO("measurement sample " << i);
        REQUIRE(r.measurement[i] == expected[i]);
        anyNonZero = anyNonZero || expected[i] != 0.0f;
    }
    REQUIRE(anyNonZero);
}

}  // namespace

TEST_CASE("soloing output 1 reaches the bus: reference is output 1's render, measurement is the "
          "SUB path alone",
          "[synthetic_loopback_multi]") {
    const auto r = record([](OutputEngine& e) { rta::measure::soloOutput(e, 1); });

    requireReference(r, 1);
    requireMeasurement(r);

    // The sub path is the only one that heard the signal: after the arm,
    // output 0's render is exact silence, so the main path contributes none of
    // it. A loopback that rendered ONE channel (every output playing channel
    // 0's block) fails here and in requireReference, as does one that ran path
    // 0 over output 1 (requireMeasurement).
    for (std::size_t i = 0; i < r.rendered; ++i) REQUIRE(r.expectedOut[0][i] == 0.0f);
}

TEST_CASE("soloing output 0 is bit-identical to the one-path code whatever the second path is",
          "[synthetic_loopback_multi]") {
    const auto r = record([](OutputEngine& e) { rta::measure::soloOutput(e, 0); });

    requireReference(r, 0);
    requireMeasurement(r);

    // The one-path expectation, with no second path in the sum at all.
    rta::measure::MeasurementPath main(kMainDelay, rta::measure::lr4HighPass(100.0, kFs));
    std::vector<float> oneOutput(r.reference.size());
    main.process(r.reference, oneOutput);
    for (std::size_t i = 0; i < oneOutput.size(); ++i) {
        INFO("measurement sample " << i);
        REQUIRE(r.measurement[i] == oneOutput[i]);
    }
}

TEST_CASE("both outputs routed: the reference is output 0's block and the measurement is the sum of "
          "both paths",
          "[synthetic_loopback_multi]") {
    const auto r = record([](OutputEngine& e) {
        rta::measure::soloOutput(e, 0);
        rta::measure::applyToggle(e, 1, true, rta::measure::SoloMode::Additive);
    });

    requireReference(r, 0);  // the LOWEST routed output
    requireMeasurement(r);
    // Both outputs really played: neither render is silent.
    bool any0 = false, any1 = false;
    for (std::size_t i = 0; i < r.rendered; ++i) {
        any0 = any0 || r.expectedOut[0][i] != 0.0f;
        any1 = any1 || r.expectedOut[1][i] != 0.0f;
    }
    REQUIRE(any0);
    REQUIRE(any1);
}
