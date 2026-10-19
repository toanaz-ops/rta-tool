// SPDX-License-Identifier: AGPL-3.0-or-later
// SWEEP lane task T6 (docs/plans/2026-09-29-sweep-ir-lane-plan.md): the SWEEP
// button against a REAL MainComponent in SYNTHETIC mode -- the OutputEngine
// loopback (wave A) carries the real sweep onto the bus, the real analysis
// thread captures it, the real worker deconvolves it. Nothing here is a stand-in
// for the state machine, only the clock in the timeout case.
//
// The sweep is shortened through MainComponentSweep::setDurationsForTest (2 s
// sweep, 0.5 s tail) so a case costs seconds. The 2 Hz timer is not pumped
// offscreen; `tickUntil` drives `timerCallback` by hand, bounded.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "rta/dsp/Biquad.h"
#include "rta/dsp/ButterworthDesign.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <functional>
#include <vector>

namespace {

using State = MainComponentSweep::State;
constexpr const char* kTag = "[main_component_sweep]";

template <typename Predicate>
bool tickUntil(MainComponent& c, Predicate&& done, int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        MainComponentTestAccess::timerTickForTest(c);
        if (done()) return true;
        juce::Thread::sleep(5);
    }
    MainComponentTestAccess::timerTickForTest(c);
    return done();
}

MainComponentSweep& sweepOf(MainComponent& c) { return MainComponentTestAccess::sweepForTest(c); }

void startSynthetic(MainComponent& c) {
    c.setSyntheticMode(true);
    sweepOf(c).setDurationsForTest(2.0, 0.5);
}

// The pure 4-sample delay plus the 40 Hz - 16 kHz band-pass the SYNTHETIC path
// puts on the measurement copy. Deconvolution is linear, so the measurement's
// impulse response is that cascade's impulse response shifted by 4; the loop's
// is the sweep's own band-limited pulse, symmetric about its peak. The expected
// delay is therefore 4 + argmax|h| of the SAME cascade the app builds, read off
// an impulse -- not off the pipeline. Tolerance 1 sample: the measurement's
// peak is the peak of h convolved with that ~1-sample-wide pulse, which can sit
// one bin off h's own when h is flat-topped, and no more.
long expectedSyntheticDelaySamples() {
    rta::dsp::BiquadCascade cascade(
        rta::dsp::ButterworthDesign::bandPass(40.0, 16000.0, 48000.0, 2).sections);
    double best = 0.0;
    long bestIndex = 0;
    for (long n = 0; n < 2048; ++n) {
        const double y = cascade.processSample(n == 0 ? 1.0 : 0.0);
        if (std::fabs(y) > best) {
            best = std::fabs(y);
            bestIndex = n;
        }
    }
    return 4 + bestIndex;
}

}  // namespace

TEST_CASE("sweep: one press adds one SWEEP trace and reads out its delay", kTag) {
    MainComponent c;
    startSynthetic(c);
    auto& library = MainComponentTestAccess::libraryForTest(c);
    REQUIRE(library.entries().empty());

    sweepOf(c).clicked();
    REQUIRE(sweepOf(c).stateForTest() == State::PreRoll);
    REQUIRE(MainComponentTestAccess::captureBusyForTest(c));

    const bool done = tickUntil(c, [&] { return sweepOf(c).stateForTest() == State::Idle; }, 60000);
    INFO("readout: " << sweepOf(c).readoutForTest());
    REQUIRE(done);

    REQUIRE(library.entries().size() == 1);
    const auto& entry = library.entries()[0];
    CHECK(entry.name.rfind("SWEEP @ ", 0) == 0);
    CHECK(entry.group == "SWEEP");
    const auto* trace = library.trace(entry.traceId);
    REQUIRE(trace != nullptr);
    CHECK(trace->has(rta::trace::Field::Phase));
    CHECK_FALSE(trace->has(rta::trace::Field::Coherence));  // never zero-filled
    CHECK(sweepOf(c).irStoreForTest().size() == 1);
    CHECK(sweepOf(c).irStoreForTest().containsForTest(entry.traceId));

    const juce::String readout = sweepOf(c).readoutForTest();
    REQUIRE(readout.startsWith("IR +"));
    CHECK(readout.contains(" samples ("));
    CHECK(readout.contains(" ms) "));
    CHECK(readout.contains(" loop "));
    CHECK(readout.endsWith(" dB"));  // no clipping warning at -12 dBFS
    const long reported = readout.fromFirstOccurrenceOf("IR ", false, false).getIntValue();
    const long expected = expectedSyntheticDelaySamples();
    INFO("reported " << reported << " expected " << expected);
    CHECK(std::labs(reported - expected) <= 1);
    CHECK_FALSE(MainComponentTestAccess::captureBusyForTest(c));
}

TEST_CASE("sweep: a SWEEP click during LOCATE is refused and adds nothing", kTag) {
    MainComponent c;
    startSynthetic(c);
    MainComponentTestAccess::locateClickedForTest(c);
    REQUIRE(MainComponentTestAccess::locateActiveForTest(c));

    sweepOf(c).clicked();

    CHECK(sweepOf(c).stateForTest() == State::Idle);
    CHECK(sweepOf(c).readoutForTest().contains("refused"));
    CHECK(MainComponentTestAccess::libraryForTest(c).entries().empty());
}

TEST_CASE("sweep: LOCATE and CAL during a SWEEP are refused, and the sweep still finishes", kTag) {
    MainComponent c;
    startSynthetic(c);
    sweepOf(c).clicked();
    REQUIRE(sweepOf(c).busy());

    MainComponentTestAccess::locateClickedForTest(c);
    MainComponentTestAccess::calibrationStartClickedForTest(c);

    CHECK_FALSE(MainComponentTestAccess::locateActiveForTest(c));
    CHECK_FALSE(MainComponentTestAccess::calibrationArmedForTest(c));
    CHECK(MainComponentTestAccess::delayReadoutForTest(c) == "delay: --");
    CHECK(MainComponentTestAccess::libraryForTest(c).entries().empty());

    // Not disturbed: the refused clicks neither re-armed the capture nor
    // swapped the source, so the sweep completes with its own trace.
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 60000));
    CHECK(sweepOf(c).readoutForTest().startsWith("IR "));
    CHECK(MainComponentTestAccess::libraryForTest(c).entries().size() == 1);
}

TEST_CASE("sweep: a second press while running says so and changes nothing", kTag) {
    MainComponent c;
    startSynthetic(c);
    sweepOf(c).clicked();
    sweepOf(c).clicked();
    CHECK(sweepOf(c).readoutForTest().contains("already running"));
    CHECK(sweepOf(c).stateForTest() == State::PreRoll);
}

TEST_CASE("sweep: an output epoch change aborts it even when the input mode ends up the same", kTag) {
    MainComponent c;
    startSynthetic(c);
    double now = 1000.0;
    sweepOf(c).setClockForTest([&] { return now; });
    sweepOf(c).clicked();
    now += 200.0;  // past the 100 ms settle
    MainComponentTestAccess::timerTickForTest(c);
    REQUIRE(sweepOf(c).stateForTest() == State::Playing);

    // SYNTHETIC off then on: the mode reads TRUE again, so only the epoch
    // (the loopback's `prepare` bumps it on the way back in, exactly as a
    // device switch does) can reveal that this is a different session.
    c.setSyntheticMode(false);
    c.setSyntheticMode(true);
    MainComponentTestAccess::timerTickForTest(c);

    CHECK(sweepOf(c).stateForTest() == State::Idle);
    CHECK(sweepOf(c).readoutForTest().contains("aborted"));
    CHECK(MainComponentTestAccess::libraryForTest(c).entries().empty());
}

TEST_CASE("sweep: turning SYNTHETIC off mid-sweep aborts it", kTag) {
    // Turning SYNTHETIC off does not bump the output epoch (only turning it on
    // does, through the loopback's prepare), so this is the mode check's case.
    MainComponent c;
    startSynthetic(c);
    double now = 1000.0;
    sweepOf(c).setClockForTest([&] { return now; });
    sweepOf(c).clicked();
    now += 200.0;
    MainComponentTestAccess::timerTickForTest(c);
    REQUIRE(sweepOf(c).stateForTest() == State::Playing);

    c.setSyntheticMode(false);
    MainComponentTestAccess::timerTickForTest(c);

    CHECK(sweepOf(c).stateForTest() == State::Idle);
    CHECK_FALSE(MainComponentTestAccess::captureBusyForTest(c));
    CHECK(sweepOf(c).readoutForTest().contains("aborted"));
}

TEST_CASE("sweep: with no REF the capture times out, says so, and disarms the source", kTag) {
    MainComponent c;
    startSynthetic(c);
    MainComponentTestAccess::channelConfigForTest(c).setRole(1, rta::platform::ChannelRole::Unused);
    double now = 1000.0;
    sweepOf(c).setClockForTest([&] { return now; });
    sweepOf(c).clicked();
    now += 200.0;
    MainComponentTestAccess::timerTickForTest(c);
    REQUIRE(sweepOf(c).stateForTest() == State::Playing);
    REQUIRE_FALSE(MainComponentTestAccess::outputSourceQuiescentForTest(c));  // it is playing

    now += 60000.0;  // far past capture length + 5 s
    MainComponentTestAccess::timerTickForTest(c);

    CHECK(sweepOf(c).stateForTest() == State::Idle);
    CHECK(sweepOf(c).readoutForTest().contains("no REF on route 1?"));
    // Disarmed: the still-running synthetic thread renders the gate down to
    // Idle, which `sourceIsQuiescent` reports. A source left armed never does.
    const auto deadline = juce::Time::getMillisecondCounter() + 5000u;
    while (!MainComponentTestAccess::outputSourceQuiescentForTest(c) &&
           juce::Time::getMillisecondCounter() < deadline) {
        juce::Thread::sleep(5);
    }
    CHECK(MainComponentTestAccess::outputSourceQuiescentForTest(c));
    CHECK(MainComponentTestAccess::libraryForTest(c).entries().empty());
}

TEST_CASE("sweep: without a device or SYNTHETIC the click is refused for the rate mismatch", kTag) {
    MainComponent c;  // LIVE, no device: the bus has no rate, the engine has its placeholder
    sweepOf(c).clicked();
    CHECK(sweepOf(c).stateForTest() == State::Idle);
    CHECK(sweepOf(c).readoutForTest().contains("sample rates differ"));
}

TEST_CASE("sweep: the SWEEP row sits under LOCATE's and overlaps nothing at 1280x800", kTag) {
    MainComponent c;
    c.setSize(1280, 800);
    c.resized();

    const auto& locate = MainComponentTestAccess::delayReadoutComponentForTest(c);
    const auto& cal = MainComponentTestAccess::calibrationStartButtonForTest(c);
    const auto& button = sweepOf(c).buttonForTest();
    const auto& readout = sweepOf(c).readoutComponentForTest();
    const auto& rail = MainComponentTestAccess::railViewportForTest(c);

    CHECK(button.getWidth() > 0);
    CHECK(readout.getWidth() > 0);
    CHECK(button.getRight() <= readout.getX());
    CHECK(readout.getRight() <= c.getWidth());
    CHECK(locate.getBottom() <= button.getY());      // under LOCATE's row...
    CHECK(button.getBottom() <= cal.getY());         // ...above CAL's
    CHECK(readout.getBottom() <= cal.getY());
    CHECK(readout.getBottom() <= rail.getY());       // and above the device rail
    CHECK(rail.getHeight() > 0);
    CHECK(rail.getBottom() <= c.getHeight());
}
