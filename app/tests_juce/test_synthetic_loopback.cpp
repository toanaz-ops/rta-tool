// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Sweep-IR lane wave A (docs/plans/2026-09-29-sweep-ir-lane-plan.md, tasks
// T1 and T2). SyntheticInput renders the OutputEngine ("loopback") and band-
// limits the measurement copy; both are proven through the REAL thread and a
// REAL CaptureBus, then LOCATE is proven through a REAL MainComponent.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "measure/OutputPolicy.h"
#include "measure/SyntheticImpairment.h"
#include "measure/SyntheticInput.h"
#include "rta/dsp/ButterworthDesign.h"
#include "rta/dsp/BiquadResponse.h"
#include "rta/gen/Oscillator.h"
#include "rta/platform/CaptureBus.h"
#include "rta/platform/OutputEngine.h"

#include <juce_core/juce_core.h>

#include <cmath>
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
constexpr double kPi = 3.14159265358979323846;
constexpr int kBlock = 512;  // SyntheticInput::Config::bufferSize default

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        if (predicate()) return true;
        juce::Thread::sleep(5);
    }
    return predicate();
}

// Same role table setSyntheticMode builds: channel 0 measures, channel 1 is
// the reference.
constexpr int kMeasurementCh = 0;
constexpr int kReferenceCh = 1;

void assignRoles(CaptureBus& bus) {
    REQUIRE(bus.config().setRole(kMeasurementCh, ChannelRole::Measurement));
    REQUIRE(bus.config().setRole(kReferenceCh, ChannelRole::Reference));
}

// Everything the bus's ring `channel` holds, from sample 0 (nothing consumes
// it in these tests, so index 0 is the first sample ever pushed).
std::vector<float> drainAll(CaptureBus& bus, int channel) {
    auto* ring = bus.ring(channel);
    REQUIRE(ring != nullptr);
    std::vector<float> all(ring->availableToRead());
    REQUIRE(ring->peek(all) == all.size());
    return all;
}

}  // namespace

TEST_CASE("an armed OutputEngine reaches the bus Reference bit-exact; the measurement is the "
          "filtered, delayed copy",
          "[synthetic_loopback]") {
    constexpr int kDelay = 4;
    // 300-3000 Hz, order 2: a filter that visibly changes a 1 kHz tone's
    // level and phase, so "the reference was filtered too" cannot hide.
    const auto filter = rta::dsp::ButterworthDesign::bandPass(300.0, 3000.0, kFs, 2).sections;
    constexpr double kToneHz = 1000.0;
    constexpr double kToneDbFsPeak = -12.0;

    CaptureBus bus(rta::platform::kFixedRingCapacitySamples);
    assignRoles(bus);
    OutputEngine engine;

    SyntheticInput::Config config;
    config.measurementDelaySamples = kDelay;
    config.measurementFilter = filter;
    config.loopback = &engine;
    auto input = std::make_unique<SyntheticInput>(bus, config);

    // The constructor prepare()d the engine; arm only afterwards (prepare
    // disarms and bumps the epoch, so an earlier setSource would render
    // silence).
    REQUIRE(engine.setSource(rta::gen::Oscillator(kFs, kToneHz, kToneDbFsPeak)));
    rta::measure::soloOutput(engine, 0);
    engine.armSource();

    REQUIRE(waitUntil([&] { return engine.renderedSamples() >= 8u * kBlock; }, 10000));
    input.reset();  // joins the thread: every rendered block has been pushed

    const std::uint64_t rendered = engine.renderedSamples();
    REQUIRE(rendered >= 8u * kBlock);
    REQUIRE(rendered % kBlock == 0);

    const auto reference = drainAll(bus, kReferenceCh);
    const auto measurement = drainAll(bus, kMeasurementCh);
    REQUIRE(reference.size() == measurement.size());
    REQUIRE(reference.size() >= rendered);

    // Expected stimulus: a second engine, same source, same chunking. The
    // bus holds pink for the blocks before the arm, then exactly the engine's
    // blocks since it -- renderedSamples() counts since the arm, so the last
    // `rendered` samples are the engine's output.
    OutputEngine expectedEngine;
    expectedEngine.prepare(kFs, 1);
    REQUIRE(expectedEngine.setSource(rta::gen::Oscillator(kFs, kToneHz, kToneDbFsPeak)));
    rta::measure::soloOutput(expectedEngine, 0);
    expectedEngine.armSource();
    std::vector<float> expected(rendered);
    for (std::size_t at = 0; at < expected.size(); at += kBlock) {
        float* out = expected.data() + at;
        expectedEngine.render(&out, 1, kBlock);
    }
    const std::size_t offset = reference.size() - static_cast<std::size_t>(rendered);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        // Bit-exact, not Approx: the loopback must be a copy, and a filter
        // wrongly applied to the Reference would move every sample.
        INFO("reference sample " << i);
        REQUIRE(reference[offset + i] == expected[i]);
    }

    // The measurement channel is the same float operations the production
    // path applies, run offline over the reference stream the bus recorded.
    rta::measure::MeasurementPath path(kDelay, filter);
    std::vector<float> expectedMeasurement(reference.size());
    path.process(reference, expectedMeasurement);
    for (std::size_t i = 0; i < reference.size(); ++i) {
        INFO("measurement sample " << i);
        REQUIRE(measurement[i] == expectedMeasurement[i]);
    }
}

TEST_CASE("a quiescent loopback engine leaves the pink noise exactly as it was",
          "[synthetic_loopback]") {
    CaptureBus busPlain(rta::platform::kFixedRingCapacitySamples);
    CaptureBus busLoop(rta::platform::kFixedRingCapacitySamples);
    assignRoles(busPlain);
    assignRoles(busLoop);
    OutputEngine engine;  // never armed

    SyntheticInput::Config plain;
    SyntheticInput::Config looped;
    looped.loopback = &engine;

    constexpr std::size_t kCompare = 4u * kBlock;
    auto a = std::make_unique<SyntheticInput>(busPlain, plain);
    auto b = std::make_unique<SyntheticInput>(busLoop, looped);
    REQUIRE(waitUntil([&] {
        return busPlain.ring(kReferenceCh)->availableToRead() >= kCompare &&
               busLoop.ring(kReferenceCh)->availableToRead() >= kCompare;
    }, 10000));
    a.reset();
    b.reset();

    const auto pinkPlain = drainAll(busPlain, kReferenceCh);
    const auto pinkLoop = drainAll(busLoop, kReferenceCh);
    for (std::size_t i = 0; i < kCompare; ++i) {
        INFO("sample " << i);
        REQUIRE(pinkPlain[i] == pinkLoop[i]);
    }
    // The engine was rendered (its clock runs) but never played anything.
    REQUIRE(engine.renderedSamples() == 0);
    REQUIRE(engine.sourceIsQuiescent());
}

TEST_CASE("SYNTHETIC + LOCATE reaches the closed-form synthetic delay", "[synthetic_loopback]") {
    MainComponent component;
    component.setSyntheticMode(true);
    MainComponentTestAccess::locateClickedForTest(component);

    // The 2 Hz timer is not pumped offscreen; drive the poll by hand. Bounded:
    // without the loopback render() call nothing advances renderedSamples()
    // and this times out -- the mutant this case exists to catch.
    const bool got = waitUntil(
        [&] {
            MainComponentTestAccess::timerTickForTest(component);
            return MainComponentTestAccess::delaySuggestionForTest(component).has_value();
        },
        30000);
    REQUIRE(got);

    const auto& suggestion = *MainComponentTestAccess::delaySuggestionForTest(component);
    INFO("verdict " << static_cast<int>(suggestion.verdict) << " samples "
                    << suggestion.best.delaySamples << " sub " << suggestion.best.subSample
                    << " trust " << suggestion.trust);
    REQUIRE(suggestion.verdict == rta::dsp::DelayVerdict::Accepted);
    // DERIVATION (plan row T2 said "4 samples"; that was wrong once decision 8
    // put a band-pass on the measurement copy): the delay PHAT sees is the
    // pure 4-sample delay plus the filter's own group delay, which for a
    // phase-weighted correlator is the mean of tau_g(f) over the bins that
    // carry signal. Mean group delay over a band equals minus the phase
    // slope across it, so it is computed here from the SAME cascade the app
    // builds, not read back from the estimate. The band is 200 Hz - 12 kHz:
    // the interior where the -30 dB noise floor does not dominate the bin.
    // Tolerance 0.5 sample: PHAT's peak is an integer-lag maximum refined by
    // a parabola (subSample, |sub| <= 0.5), so half a sample is the
    // resolution of the estimator itself, not slack.
    const auto sections =
        rta::dsp::ButterworthDesign::bandPass(40.0, 16000.0, kFs, 2).sections;
    constexpr double kLoHz = 200.0;
    constexpr double kHiHz = 12000.0;
    constexpr int kSteps = 4000;
    double phaseChange = 0.0;
    double previous = std::arg(rta::dsp::cascadeResponse(sections, 2.0 * kPi * kLoHz / kFs));
    for (int i = 1; i <= kSteps; ++i) {
        const double hz = kLoHz + (kHiHz - kLoHz) * i / kSteps;
        const double phase = std::arg(rta::dsp::cascadeResponse(sections, 2.0 * kPi * hz / kFs));
        double step = phase - previous;
        step -= 2.0 * kPi * std::round(step / (2.0 * kPi));  // unwrap, step << pi
        phaseChange += step;
        previous = phase;
    }
    const double meanGroupDelaySamples =
        -phaseChange / (2.0 * kPi * (kHiHz - kLoHz) / kFs);
    const double expectedDelay = 4.0 + meanGroupDelaySamples;
    const double estimate =
        static_cast<double>(suggestion.best.delaySamples) + suggestion.best.subSample;
    INFO("expected " << expectedDelay << " estimate " << estimate);
    REQUIRE(meanGroupDelaySamples > 0.0);
    REQUIRE(std::abs(estimate - expectedDelay) <= 0.5);
}
