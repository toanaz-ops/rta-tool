// SPDX-License-Identifier: AGPL-3.0-or-later
// H7 task T13 (docs/plans/2026-10-21-h7-generator-output-plan.md, owner decision
// D1, wave B1): FOLLOW GEN, the default measurement-level mode. LOCATE, VERIFY,
// SWEEP (rail, ALIGN's and RT60's presses) and ALIGN build their excitation at
// the generator's level SETTING -- GEN need not be playing -- clamped by the same
// -12 dBFS ceiling; LOCATE, VERIFY and the rail SWEEP also route the generator's
// outputs, ALIGN and RT60 keep theirs. Every readout and stored name carries the
// level and outputs used. Against a REAL MainComponent.
//
// LOCATE is read off the engine itself: with no loopback thread rendering (LIVE,
// no device) the test renders the armed engine by hand and measures the RMS, so
// the source's level is a measurement, not a setting read back.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using Access = MainComponentTestAccess;
using rta::platform::OutputRole;
constexpr const char* kTag = "[main_component_generator_follow]";

template <typename Predicate>
bool tickUntil(MainComponent& c, Predicate&& done, int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        Access::timerTickForTest(c);
        if (done()) return true;
        juce::Thread::sleep(5);
    }
    Access::timerTickForTest(c);
    return done();
}

MainComponentGenerator& genOf(MainComponent& c) { return Access::generatorForTest(c); }
MainComponentSweep& sweepOf(MainComponent& c) { return Access::sweepForTest(c); }

void startSynthetic(MainComponent& c) {
    c.setSyntheticMode(true);
    Access::waitForResyncForTest(c);
    sweepOf(c).setDurationsForTest(2.0, 0.5);
    // The capture timeout runs on the wall clock while the loopback is paced by it too, so a
    // box shared with other builds aborts a healthy sweep; stretch the slack as H5's specimens do.
    sweepOf(c).setTimeoutSlackForTest(30000.0);
}

/// GEN's outputs set to exactly `channel` (zero-based), while stopped.
void genOnly(MainComponent& c, int channel) {
    genOf(c).outputToggled(channel);
    for (int ch = 0; ch < 8; ++ch) {
        if (ch != channel) {
            auto outs = genOf(c).settingsForTest().outputs;
            if (std::find(outs.begin(), outs.end(), ch) != outs.end()) genOf(c).outputToggled(ch);
        }
    }
    REQUIRE(genOf(c).settingsForTest().outputs == std::vector<int>{channel});
}

struct Levels {
    double out0Db = -1000.0;
    double out1Db = -1000.0;
};

/// Renders the engine by hand (nothing else drives it) and reads each output's RMS.
Levels renderRms(MainComponent& c) {
    auto& engine = Access::outputEngineForTest(c);
    constexpr int kBlock = 512;
    constexpr int kBlocks = 512;  // 2^18 samples
    double acc0 = 0.0, acc1 = 0.0;
    std::size_t counted = 0;
    for (int b = 0; b < kBlocks; ++b) {
        std::vector<float> left(kBlock, 0.0f), right(kBlock, 0.0f);
        float* outs[2] = {left.data(), right.data()};
        engine.render(outs, 2, kBlock);
        if (b < 8) continue;  // the 10 ms gate ramp
        for (int i = 0; i < kBlock; ++i) {
            acc0 += static_cast<double>(left[static_cast<std::size_t>(i)]) * left[static_cast<std::size_t>(i)];
            acc1 += static_cast<double>(right[static_cast<std::size_t>(i)]) * right[static_cast<std::size_t>(i)];
        }
        counted += kBlock;
    }
    const auto db = [&](double acc) { return acc > 0.0 ? 10.0 * std::log10(acc / static_cast<double>(counted)) : -1000.0; };
    return {db(acc0), db(acc1)};
}

/// A fresh LIVE component whose engine nobody else renders.
void quietLive(MainComponent& c) { Access::outputEngineForTest(c).prepare(48000.0, 2); }

}  // namespace

// --- LOCATE: read off the engine -------------------------------------------------

TEST_CASE("T13 LOCATE: the source is pink at GEN's level, -30 by default, not the old -12", kTag) {
    MainComponent c;
    quietLive(c);
    Access::locateClickedForTest(c);  // GEN is stopped: following its SETTING, not its sound
    REQUIRE(Access::locateActiveForTest(c));
    const Levels heard = renderRms(c);
    INFO("out 1 " << heard.out0Db << " dBFS RMS");
    // 0.5 dB, not the 0.2 of the fixed-seed model test: LOCATE seeds from the clock, and
    // the RMS of pink noise over 2^18 samples varies a little by seed. -12 would be 18 dB away.
    CHECK(std::abs(heard.out0Db - (-30.0)) <= 0.5);
    CHECK(heard.out1Db < -100.0);  // out 2 silent: GEN's outputs are {out 1}
}

TEST_CASE("T13 LOCATE: a stepped GEN level is followed, and the -12 ceiling still bounds it", kTag) {
    MainComponent c;
    quietLive(c);
    for (int i = 0; i < 10; ++i) genOf(c).stepClicked(+1);  // -30 -> -20
    Access::locateClickedForTest(c);
    CHECK(std::abs(renderRms(c).out0Db - (-20.0)) <= 0.5);
    CHECK(Access::delayReadoutForTest(c).contains("@ -20.0 dBFS RMS, out 1"));

    MainComponent loud;
    quietLive(loud);
    Access::pinGeneratorLevelForTest(loud, 0.0);  // a request above the ceiling
    CHECK(genOf(loud).settingsForTest().levelDb == -12.0);
    Access::locateClickedForTest(loud);
    CHECK(std::abs(renderRms(loud).out0Db - (-12.0)) <= 0.5);
}

TEST_CASE("T13 LOCATE: it plays on GEN's ticked outputs, not always out 1", kTag) {
    MainComponent c;
    quietLive(c);
    genOnly(c, 1);  // out 2 only
    Access::locateClickedForTest(c);
    REQUIRE(Access::locateActiveForTest(c));
    CHECK(Access::outputRoleForTest(c, 0) != OutputRole::Routed);
    CHECK(Access::outputRoleForTest(c, 1) == OutputRole::Routed);
    const Levels heard = renderRms(c);
    CHECK(heard.out0Db < -100.0);
    CHECK(std::abs(heard.out1Db - (-30.0)) <= 0.5);
    CHECK(Access::delayReadoutForTest(c).contains("@ -30.0 dBFS RMS, out 2"));
}

TEST_CASE("T13 LOCATE: both ticked outputs play (additive, not a solo)", kTag) {
    MainComponent c;
    quietLive(c);
    genOf(c).outputToggled(1);  // out 1 + out 2
    Access::locateClickedForTest(c);
    CHECK(Access::outputRoleForTest(c, 0) == OutputRole::Routed);
    CHECK(Access::outputRoleForTest(c, 1) == OutputRole::Routed);
    CHECK(Access::delayReadoutForTest(c).contains("out 1+2"));
}

TEST_CASE("T13: the one answer a measurement asks for follows GEN's settings", kTag) {
    MainComponent c;
    startSynthetic(c);
    auto drive = Access::measurementDriveForTest(c);
    CHECK(drive.levelDb == -30.0);
    CHECK(drive.outputs == std::vector<int>{0});
    genOf(c).stepClicked(+1);
    genOf(c).outputToggled(1);
    drive = Access::measurementDriveForTest(c);
    CHECK(drive.levelDb == -29.0);
    CHECK(drive.outputs == std::vector<int>{0, 1});
}

// --- SWEEP: rail, ALIGN's and RT60's presses -------------------------------------

TEST_CASE("T13 SWEEP: the rail button sweeps at GEN's level, on GEN's outputs, and names both", kTag) {
    MainComponent c;
    startSynthetic(c);
    genOnly(c, 1);
    sweepOf(c).clicked();
    REQUIRE(sweepOf(c).busy());
    CHECK(sweepOf(c).armedLevelDbForTest() == -30.0);  // the old fixed level was -12
    CHECK(sweepOf(c).readoutForTest() == "sweep: arming @ -30.0 dBFS peak, out 2...");
    CHECK(Access::outputRoleForTest(c, 0) != OutputRole::Routed);
    CHECK(Access::outputRoleForTest(c, 1) == OutputRole::Routed);

    // Out 2 is the 100 Hz low-pass: the loopback's own band-limiting can refuse the
    // analysis, as it does at -12 too. Whichever it is, the readout names level and output.
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 40000));
    INFO(sweepOf(c).readoutForTest().toStdString());
    CHECK(sweepOf(c).readoutForTest().contains("@ -30.0 dBFS peak, out 2"));
}

TEST_CASE("T13 SWEEP: a finished sweep reads out and stores its level and output", kTag) {
    MainComponent c;
    startSynthetic(c);
    sweepOf(c).clicked();
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 40000));
    const auto readout = sweepOf(c).readoutForTest();
    INFO(readout.toStdString());
    REQUIRE(readout.startsWith("IR "));  // -30 dBFS peak is enough for the SYNTHETIC loop
    CHECK(readout.contains("SWEEP @ -30.0 dBFS peak, out 1"));
    const auto& entries = Access::libraryForTest(c).entries();
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].name.rfind("SWEEP @ ", 0) == 0);
    CHECK(juce::String(entries[0].name).endsWith("(-30.0 dBFS peak, out 1)"));  // the stored record
}

TEST_CASE("T13 SWEEP: a quiet sweep either works or refuses -- and the readout names its level", kTag) {
    MainComponent c;
    startSynthetic(c);
    for (int i = 0; i < 30; ++i) genOf(c).stepClicked(-1);  // -60.0, the floor
    REQUIRE(genOf(c).settingsForTest().levelDb == -60.0);
    sweepOf(c).clicked();
    REQUIRE(sweepOf(c).busy());
    CHECK(sweepOf(c).armedLevelDbForTest() == -60.0);
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 40000));
    // Whatever the verdict at -60 dBFS, it is not an unexplained one.
    CHECK(sweepOf(c).readoutForTest().contains("@ -60.0 dBFS peak, out 1"));
}

TEST_CASE("T13 SWEEP: ALIGN's and RT60's named presses follow the level but keep their own output", kTag) {
    MainComponent c;
    startSynthetic(c);
    genOnly(c, 0);  // GEN says out 1 ...
    REQUIRE(sweepOf(c).clickedOnOutput(1, MainComponentSweep::Requester::Rt60));  // ... RT60 asked for out 2
    CHECK(sweepOf(c).armedLevelDbForTest() == -30.0);
    CHECK(sweepOf(c).readoutForTest() == "sweep: arming @ -30.0 dBFS peak, out 2...");
    CHECK(Access::outputRoleForTest(c, 0) != OutputRole::Routed);
    CHECK(Access::outputRoleForTest(c, 1) == OutputRole::Routed);
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 40000));
    CHECK(sweepOf(c).readoutForTest().contains("@ -30.0 dBFS peak, out 2"));
}

// --- ALIGN -------------------------------------------------------------------------

TEST_CASE("T13 ALIGN: the level follows GEN, main and sub stay the answers' outputs", kTag) {
    MainComponent c;
    startSynthetic(c);
    genOnly(c, 1);  // GEN ticked out 2; ALIGN's MAIN is out 1
    auto& runner = Access::alignForTest(c).runnerForTest();
    auto& answers = runner.answers();
    answers.mainOutput = 0;
    answers.subOutput = 1;
    answers.highPass = rta::measure::Source::Main;
    answers.topology = rta::measure::Topology{rta::measure::CrossoverFamily::LinkwitzRiley, 4};
    answers.inversion = rta::measure::ProcessorInversion::No;
    answers.seedAnswered = true;
    REQUIRE(tickUntil(c, [&] { return runner.blocker().kind == rta::measure::AlignBlock::None; }, 30000));

    REQUIRE(runner.press());
    CHECK(runner.status().find("@ -30.0 dBFS RMS") != std::string::npos);
    CHECK(Access::outputRoleForTest(c, 0) == OutputRole::Routed);   // MAIN solo, whatever GEN ticked
    CHECK(Access::outputRoleForTest(c, 1) != OutputRole::Routed);
}

// --- readouts and hints name the level ---------------------------------------------

TEST_CASE("T13 LOCATE: the readout names the level while locating and after, even when it is quiet", kTag) {
    MainComponent c;
    startSynthetic(c);
    for (int i = 0; i < 30; ++i) genOf(c).stepClicked(-1);  // -60.0
    Access::locateClickedForTest(c);
    CHECK(Access::delayReadoutForTest(c) == "delay: locating @ -60.0 dBFS RMS, out 1...");
    REQUIRE(tickUntil(c, [&] { return Access::delaySuggestionForTest(c).has_value(); }, 40000));
    // Accepted or refused for want of signal, the answer is read at a stated level.
    CHECK(Access::delayReadoutForTest(c).contains("@ -60.0 dBFS RMS, out 1"));
}

TEST_CASE("T13 hints: LOCATE and SWEEP name the level a press would use in the current mode", kTag) {
    MainComponent c;
    startSynthetic(c);
    Access::timerTickForTest(c);
    CHECK(Access::locateHintForTest(c).contains("-30.0 dBFS RMS"));
    CHECK(sweepOf(c).buttonForTest().getProperties()[az::ui::hintProperty].toString().contains("-30.0 dBFS peak"));

    for (int i = 0; i < 10; ++i) genOf(c).stepClicked(+1);
    Access::timerTickForTest(c);
    CHECK(Access::locateHintForTest(c).contains("-20.0 dBFS RMS"));
    CHECK(sweepOf(c).buttonForTest().getProperties()[az::ui::hintProperty].toString().contains("-20.0 dBFS peak"));
}
