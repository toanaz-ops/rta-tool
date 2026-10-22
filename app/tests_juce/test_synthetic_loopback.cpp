// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Sweep-IR lane wave A (docs/plans/2026-09-29-sweep-ir-lane-plan.md, tasks
// T1 and T2). SyntheticInput renders the OutputEngine ("loopback") and band-
// limits the measurement copy; both are proven through the REAL thread and a
// REAL CaptureBus, then LOCATE is proven through a REAL MainComponent.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "measure/Levels.h"
#include "measure/OutputPolicy.h"
#include "measure/SyntheticImpairment.h"
#include "measure/SyntheticInput.h"
#include "rta/dsp/ButterworthDesign.h"
#include "rta/gen/Oscillator.h"
#include "rta/gen/Synthetic.h"
#include "rta/platform/CaptureBus.h"
#include "rta/platform/OutputEngine.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <vector>

using rta::measure::SyntheticInput;
using rta::platform::CaptureBus;
using rta::platform::ChannelRole;
using rta::platform::OutputEngine;

namespace {

constexpr double kFs = 48000.0;
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

TEST_CASE("a quiescent loopback engine leaves the generator exactly as it was",
          "[synthetic_loopback]") {
    // Expectation: the SAME deterministic generator SyntheticInput builds
    // (rta/gen/Synthetic.h), rendered here in 512-sample blocks -- an
    // independent oracle, so a deleted render call (stale zeros) cannot hide
    // by being identical in both of two SyntheticInputs.
    const SyntheticInput::Config defaults;
    const double targetDbFs =
        rta::measure::levelDbFs(defaults.amplitude * defaults.amplitude * 0.5);
    constexpr std::size_t kCompare = 4u * kBlock;

    for (const auto source : {SyntheticInput::Config::Source::PinkNoise,
                              SyntheticInput::Config::Source::Sine}) {
        CaptureBus bus(rta::platform::kFixedRingCapacitySamples);
        assignRoles(bus);
        OutputEngine engine;  // never armed

        SyntheticInput::Config config;
        config.source = source;
        config.loopback = &engine;
        auto input = std::make_unique<SyntheticInput>(bus, config);
        REQUIRE(waitUntil([&] { return bus.ring(kReferenceCh)->availableToRead() >= kCompare; },
                          10000));
        input.reset();

        std::vector<float> expected(kCompare);
        if (source == SyntheticInput::Config::Source::Sine) {
            rta::gen::SyntheticSine sine(config.sampleRate, config.sineHz, targetDbFs);
            for (std::size_t at = 0; at < kCompare; at += kBlock) {
                sine.render(std::span<float>(expected).subspan(at, kBlock));
            }
        } else {
            rta::gen::SyntheticPink pink(kBlock, targetDbFs, config.seed);
            for (std::size_t at = 0; at < kCompare; at += kBlock) {
                pink.render(std::span<float>(expected).subspan(at, kBlock));
            }
        }
        const auto reference = drainAll(bus, kReferenceCh);
        bool anyNonZero = false;
        for (std::size_t i = 0; i < kCompare; ++i) {
            INFO("source " << static_cast<int>(source) << " sample " << i);
            REQUIRE(reference[i] == expected[i]);
            anyNonZero = anyNonZero || expected[i] != 0.0f;
        }
        REQUIRE(anyNonZero);  // not vacuous: the oracle itself is a signal
        // The engine's clock ran (render was called) but nothing was played.
        REQUIRE(engine.renderedSamples() == 0);
        REQUIRE(engine.sourceIsQuiescent());
    }
}

TEST_CASE("constructing with a loopback prepares the engine at the synthetic rate",
          "[synthetic_loopback]") {
    // 44.1 kHz, not the engine's 48 kHz placeholder, so a deleted prepare()
    // is visible. prepare() also bumps the epoch, which is what disarms any
    // source armed against the previous device.
    CaptureBus bus(rta::platform::kFixedRingCapacitySamples);
    assignRoles(bus);
    OutputEngine engine;
    const std::uint64_t epochBefore = engine.outputEpoch();

    SyntheticInput::Config config;
    config.sampleRate = 44100.0;
    config.loopback = &engine;
    auto input = std::make_unique<SyntheticInput>(bus, config);
    input.reset();

    REQUIRE(engine.sampleRate() == 44100.0);
    REQUIRE(engine.outputEpoch() == epochBefore + 1);
}

TEST_CASE("SYNTHETIC + LOCATE reaches the closed-form synthetic delay", "[synthetic_loopback]") {
    MainComponent component;
    component.setSyntheticMode(true);
    // H7 T13: measurements follow GEN's level by default (-30.0); this fixture was settled at the
    // record's -12 dBFS, so it pins GEN there and keeps testing what it tested.
    MainComponentTestAccess::pinGeneratorLevelForTest(component, -12.0);
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
    // EXPECTED VALUE: 4.76 samples = the pure 4-sample delay plus the
    // measurement band-pass's own delay as GCC-PHAT sees it over DC..Nyquist
    // (MainComponentDelay.cpp leaves PhatOptions minHz = maxHz = 0). The plan
    // row T2 said "4 samples"; that ignored the filter added by decision 8.
    //
    // PROVENANCE -- an EXTERNAL numeric oracle, NOT a closed form. An earlier
    // version of this comment derived 4.75 as a mean group delay over a
    // hand-picked 200 Hz - 12 kHz band; PHAT does not average over that band,
    // so that derivation was wrong and the agreement was luck. (An earlier
    // revision also said the same formula over DC..Nyquist gives 6.26; that is
    // not reproducible -- the phase-slope mean group delay of this band-pass
    // over DC..Nyquist is 0.50 samples, 4.50 with the delay, also not 4.76:
    // PHAT is a whitened correlation peak, not a mean group delay.) The value below comes from an
    // independent script (the verifier's review of PRs #67-#69): pink noise, a 4-sample delay,
    // butter(2, [40, 16000]) band-pass at fs = 48 kHz, -30 dB white noise,
    // GCC-PHAT with regularisation 1e-10 and parabolic refinement, which read
    // 4.756 - 4.766 across seeds. It is therefore a regression lock against
    // that oracle (CLAUDE.md verification standard, "label it as such"), not a
    // textbook identity.
    //
    // Tolerance 0.5 sample: PHAT's peak is an integer-lag maximum refined by a
    // parabola (subSample, |sub| <= 0.5), so half a sample is the resolution
    // of the estimator itself, not slack. A missing filter (estimate 4.0) is
    // 0.76 away, outside it, so that mutant goes RED.
    constexpr double kExternalOracleSamples = 4.76;
    const double estimate =
        static_cast<double>(suggestion.best.delaySamples) + suggestion.best.subSample;
    INFO("external oracle " << kExternalOracleSamples << " estimate " << estimate);
    REQUIRE(std::abs(estimate - kExternalOracleSamples) <= 0.5);
}

TEST_CASE("leaving SYNTHETIC mid-LOCATE cancels it instead of refusing LOCATE for good",
          "[synthetic_loopback]") {
    // Before cancelLocate(), the waiting/armed flags survived the mode change
    // and locateClicked() returned early on every later click (captureBusy()).
    MainComponent component;
    component.setSyntheticMode(true);
    MainComponentTestAccess::locateClickedForTest(component);
    REQUIRE(MainComponentTestAccess::locateActiveForTest(component));

    component.setSyntheticMode(false);
    CHECK_FALSE(MainComponentTestAccess::locateActiveForTest(component));
    CHECK_FALSE(MainComponentTestAccess::captureBusyForTest(component));

    // A fresh LOCATE is accepted again after re-entering the mode.
    component.setSyntheticMode(true);
    MainComponentTestAccess::locateClickedForTest(component);
    CHECK(MainComponentTestAccess::locateActiveForTest(component));
}

TEST_CASE("leaving SYNTHETIC mid-LOCATE disarms the excitation: the engine falls quiet", "[synthetic_loopback]") {
    // H5 closeout C2. cancelLocate() calls disarmSource(); with that call deleted
    // the pink noise stays armed, the engine never reaches quiescence, and every
    // later feature that needs setSource() (ALIGN, SWEEP, VERIFY) is refused. The
    // case above only reads the flags, so it stayed green without the disarm.
    MainComponent component;
    component.setSyntheticMode(true);
    MainComponentTestAccess::locateClickedForTest(component);
    REQUIRE(MainComponentTestAccess::locateActiveForTest(component));
    REQUIRE_FALSE(MainComponentTestAccess::outputSourceQuiescentForTest(component));  // armed

    component.setSyntheticMode(false);  // the loopback thread is gone; nothing renders any more
    // Drive the engine the way a device callback would: disarmed, the master gate
    // ramps out and publishes idle; still armed, it never does.
    auto& engine = MainComponentTestAccess::outputEngineForTest(component);
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    float* outs[2] = { left.data(), right.data() };
    for (int block = 0; block < 32; ++block) engine.render(outs, 2, 512);
    CHECK(engine.sourceIsQuiescent());
    CHECK(MainComponentTestAccess::outputSourceQuiescentForTest(component));
}

TEST_CASE("a channel-role change mid-LOCATE resyncs the input and cancels the capture", "[synthetic_loopback]") {
    // A resync discards samples the delay is measured from; without
    // MainComponentDelay's sequence check LOCATE would suggest a shifted delay.
    MainComponent component;
    component.setSyntheticMode(true);
    MainComponentTestAccess::waitForResyncForTest(component);
    MainComponentTestAccess::locateClickedForTest(component);

    const bool armed = waitUntil(
        [&] {
            MainComponentTestAccess::timerTickForTest(component);
            return MainComponentTestAccess::locateCaptureArmedForTest(component);
        },
        10000);
    REQUIRE(armed);

    REQUIRE(MainComponentTestAccess::channelConfigForTest(component).setRole(2, ChannelRole::Measurement));
    const bool cancelled = waitUntil(
        [&] {
            MainComponentTestAccess::timerTickForTest(component);
            return !MainComponentTestAccess::locateActiveForTest(component);
        },
        10000);
    REQUIRE(cancelled);
    CHECK(MainComponentTestAccess::delayReadoutForTest(component).contains("resynced"));
    CHECK_FALSE(MainComponentTestAccess::delaySuggestionForTest(component).has_value());
}
