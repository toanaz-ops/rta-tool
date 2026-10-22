// SPDX-License-Identifier: AGPL-3.0-or-later
// H7 wave B2, task T14: the MEAS toggle and the OWN measurement level
// (docs/plans/2026-10-21-h7-generator-output-plan.md, owner decisions D1-D3).
//
// OWN means a measurement plays at ITS OWN level (default -12 dBFS, the same
// [-60, -12] range and 1 dB step as the generator, clamped) and keeps its own
// output, whatever the generator is set to. These cases read the level off the
// engine or the armed request, so a mode that is ignored cannot pass.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using Access = MainComponentTestAccess;
using rta::measure::MeasurementLevelMode;
using rta::platform::OutputRole;
constexpr const char* kTag = "[main_component_generator_own]";

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
    sweepOf(c).setTimeoutSlackForTest(30000.0);  // the capture runs on the wall clock; see the follow tests
}

/// Renders the engine by hand and reads the RMS of each of the two outputs.
std::pair<double, double> renderRms(MainComponent& c) {
    auto& engine = Access::outputEngineForTest(c);
    constexpr int kBlock = 512;
    constexpr int kBlocks = 512;
    double acc0 = 0.0, acc1 = 0.0;
    std::size_t counted = 0;
    for (int b = 0; b < kBlocks; ++b) {
        std::vector<float> left(kBlock, 0.0f), right(kBlock, 0.0f);
        float* outs[2] = {left.data(), right.data()};
        engine.render(outs, 2, kBlock);
        if (b < 8) continue;  // the gate ramp
        for (int i = 0; i < kBlock; ++i) {
            acc0 += static_cast<double>(left[static_cast<std::size_t>(i)]) * left[static_cast<std::size_t>(i)];
            acc1 += static_cast<double>(right[static_cast<std::size_t>(i)]) * right[static_cast<std::size_t>(i)];
        }
        counted += kBlock;
    }
    const auto db = [&](double acc) { return acc > 0.0 ? 10.0 * std::log10(acc / static_cast<double>(counted)) : -1000.0; };
    return {db(acc0), db(acc1)};
}

void quietLive(MainComponent& c) { Access::outputEngineForTest(c).prepare(48000.0, 2); }

void ownMode(MainComponent& c) {
    genOf(c).measurementModeClicked();
    REQUIRE(genOf(c).measurementModeForTest() == MeasurementLevelMode::Own);
}

void stepOwn(MainComponent& c, int steps) {
    for (int i = 0; i < std::abs(steps); ++i) genOf(c).ownStepClicked(steps > 0 ? +1 : -1);
}

}  // namespace

TEST_CASE("T14: every launch starts in FOLLOW GEN, OWN at -12, nothing persisted (D3)", kTag) {
    MainComponent c;
    CHECK(genOf(c).measurementModeForTest() == MeasurementLevelMode::FollowGen);
    CHECK(genOf(c).measButtonTextForTest() == "FOLLOW GEN");
    CHECK(genOf(c).ownLevelDbForTest() == -12.0);
    CHECK_FALSE(genOf(c).ownMinusButtonForTest().isVisible());
    CHECK_FALSE(genOf(c).ownPlusButtonForTest().isVisible());
    // Change everything, drop the component, make a new one: it is back at the defaults.
    ownMode(c);
    stepOwn(c, -20);
    MainComponent fresh;
    CHECK(genOf(fresh).measurementModeForTest() == MeasurementLevelMode::FollowGen);
    CHECK(genOf(fresh).ownLevelDbForTest() == -12.0);
}

TEST_CASE("T14: the MEAS button toggles the mode and names the OWN level, one decimal", kTag) {
    MainComponent c;
    genOf(c).measurementModeClicked();
    CHECK(genOf(c).measButtonTextForTest() == "OWN -12.0");
    stepOwn(c, -18);
    CHECK(genOf(c).measButtonTextForTest() == "OWN -30.0");
    genOf(c).measurementModeClicked();
    CHECK(genOf(c).measButtonTextForTest() == "FOLLOW GEN");
    genOf(c).measurementModeClicked();  // back to OWN: the level it left at, not a reset
    CHECK(genOf(c).measButtonTextForTest() == "OWN -30.0");
}

TEST_CASE("T14: the OWN level clamps to [-60, -12] in 1 dB steps", kTag) {
    MainComponent c;
    ownMode(c);
    stepOwn(c, +5);
    CHECK(genOf(c).ownLevelDbForTest() == -12.0);  // the ceiling
    stepOwn(c, -1);
    CHECK(genOf(c).ownLevelDbForTest() == -13.0);
    stepOwn(c, -100);
    CHECK(genOf(c).ownLevelDbForTest() == -60.0);  // the floor
    CHECK(Access::measurementDriveForTest(c).levelDb == -60.0);
}

TEST_CASE("T14: MEAS and the OWN steppers are 44 px touch targets in the masthead", kTag) {
    MainComponent c;
    c.setSize(1280, 800);
    c.resized();
    ownMode(c);  // re-lays the strip with the steppers in it
    auto& g = genOf(c);
    for (const juce::Component* button : std::initializer_list<const juce::Component*>{
             &g.measButtonForTest(), &g.ownMinusButtonForTest(), &g.ownPlusButtonForTest()}) {
        CHECK(button->isVisible());
        CHECK(button->getHeight() >= az::ui::touchTarget);
        CHECK(button->getWidth() >= az::ui::touchTarget);
        CHECK(button->getBottom() <= az::ui::transportHeight);
    }
    // The three strip buttons never overlap what comes before them.
    CHECK(g.ownMinusButtonForTest().getX() >= g.measButtonForTest().getRight());
}

TEST_CASE("T14 LOCATE in OWN: its own level and out 1, whatever GEN is set to", kTag) {
    MainComponent c;
    quietLive(c);
    genOf(c).outputToggled(1);  // GEN ticks out 1 + out 2 ...
    genOf(c).outputToggled(0);  // ... then only out 2
    for (int i = 0; i < 30; ++i) genOf(c).stepClicked(-1);  // GEN at -60
    ownMode(c);
    stepOwn(c, -8);  // OWN -20
    Access::locateClickedForTest(c);
    REQUIRE(Access::locateActiveForTest(c));
    const auto heard = renderRms(c);
    INFO("out 1 " << heard.first << ", out 2 " << heard.second);
    CHECK(std::abs(heard.first - (-20.0)) <= 0.5);  // OWN level, not GEN's -60
    CHECK(heard.second < -100.0);                   // its own output (out 1), not GEN's out 2
    CHECK(Access::delayReadoutForTest(c).contains("@ -20.0 dBFS RMS"));
}

TEST_CASE("T14 SWEEP in OWN: the sweep is armed at the OWN level and the readout names it", kTag) {
    MainComponent c;
    startSynthetic(c);
    genOf(c).stepClicked(-1);  // GEN -31: must not matter
    ownMode(c);
    stepOwn(c, -6);  // OWN -18
    sweepOf(c).clicked();
    REQUIRE(sweepOf(c).busy());
    CHECK(sweepOf(c).armedLevelDbForTest() == -18.0);
    CHECK(sweepOf(c).readoutForTest().startsWith("sweep: arming @ -18.0 dBFS peak"));
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 40000));
    CHECK(sweepOf(c).readoutForTest().contains("@ -18.0 dBFS peak"));
}

TEST_CASE("T14 SWEEP in OWN works at -12 while GEN sits at the -60 floor (FOLLOW GEN at -60 is quiet)", kTag) {
    // The point of the mode: a measurement that needs level (out 1's band-pass loop at -12 gives
    // a clean IR) is not held hostage by a generator turned right down for the room.
    MainComponent c;
    startSynthetic(c);
    for (int i = 0; i < 30; ++i) genOf(c).stepClicked(-1);
    ownMode(c);  // OWN stays at its -12 default
    REQUIRE(Access::measurementDriveForTest(c).levelDb == -12.0);
    sweepOf(c).clicked();
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 40000));
    INFO(sweepOf(c).readoutForTest().toStdString());
    CHECK(sweepOf(c).readoutForTest().startsWith("IR "));
}

TEST_CASE("T14: switching modes flips the drive every measurement asks for", kTag) {
    MainComponent c;
    quietLive(c);
    genOf(c).stepClicked(-5);  // GEN -35
    CHECK(Access::measurementDriveForTest(c).levelDb == -35.0);
    ownMode(c);
    stepOwn(c, -3);  // OWN -15
    CHECK(Access::measurementDriveForTest(c).levelDb == -15.0);
    CHECK(Access::measurementDriveForTest(c).outputs.empty());
    genOf(c).measurementModeClicked();
    CHECK(Access::measurementDriveForTest(c).levelDb == -35.0);
    CHECK(Access::measurementDriveForTest(c).outputs == std::vector<int>{0});
}

TEST_CASE("T14: the hints name the level in the current mode", kTag) {
    MainComponent c;
    Access::timerTickForTest(c);
    CHECK(Access::locateHintForTest(c).contains("-30.0 dBFS RMS"));
    ownMode(c);
    stepOwn(c, -4);  // -16
    Access::timerTickForTest(c);
    CHECK(Access::locateHintForTest(c).contains("-16.0 dBFS RMS"));
}

TEST_CASE("T14: a toggle while a measurement runs is refused; the run keeps its level, the next press follows", kTag) {
    MainComponent c;
    startSynthetic(c);
    sweepOf(c).clicked();
    REQUIRE(sweepOf(c).busy());
    CHECK(sweepOf(c).armedLevelDbForTest() == -30.0);  // FOLLOW GEN default
    genOf(c).measurementModeClicked();                 // refused: SWEEP holds the output
    CHECK(genOf(c).measurementModeForTest() == MeasurementLevelMode::FollowGen);
    // The refusal outlives the 2 Hz poll (the old code reset the readout on the first tick).
    for (int i = 0; i < 4; ++i) Access::timerTickForTest(c);
    REQUIRE(sweepOf(c).busy());
    CHECK(genOf(c).readoutForTest() == "MEAS: not while SWEEP runs (applies to the next press)");
    CHECK(sweepOf(c).armedLevelDbForTest() == -30.0);
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 40000));

    genOf(c).measurementModeClicked();  // idle: accepted
    CHECK(genOf(c).measurementModeForTest() == MeasurementLevelMode::Own);
    sweepOf(c).clicked();
    REQUIRE(sweepOf(c).busy());
    CHECK(sweepOf(c).armedLevelDbForTest() == -12.0);
    genOf(c).ownStepClicked(-1);  // the OWN stepper is refused while it runs, too
    CHECK(genOf(c).ownLevelDbForTest() == -12.0);
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 40000));
}

TEST_CASE("T14: a MEAS refusal clears at the operator's next GEN or MEAS action, and when the holder lets go", kTag) {
    MainComponent c;
    startSynthetic(c);
    sweepOf(c).clicked();
    REQUIRE(sweepOf(c).busy());
    genOf(c).ownStepClicked(-1);  // refused
    CHECK(genOf(c).readoutForTest().startsWith("MEAS: not while SWEEP runs"));
    genOf(c).stepClicked(-1);  // an action on GEN: the refusal is the operator's old news
    CHECK_FALSE(genOf(c).readoutForTest().startsWith("MEAS: not while"));

    genOf(c).measurementModeClicked();  // refused again, then SWEEP finishes
    CHECK(genOf(c).readoutForTest().startsWith("MEAS: not while SWEEP runs"));
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 40000));
    Access::timerTickForTest(c);
    CHECK_FALSE(genOf(c).readoutForTest().startsWith("MEAS: not while"));
}

TEST_CASE("T14 rail: OWN mode keeps the strip on screen, 44 px, and the longest readout unclipped", "[main_component_rail]") {
    // Mirrors PR #88's rail test for FOLLOW GEN. In OWN the two extra steppers narrow the readout
    // below 300 px, so it is allowed two lines -- what is pinned is that the longest text still fits.
    const juce::Point<int> sizes[] = {{1280, 800}, {1300, 820}};
    for (const auto size : sizes) {
        CAPTURE(size.x, size.y);
        MainComponent c;
        c.setSize(size.x, size.y);
        c.resized();
        ownMode(c);
        auto& g = genOf(c);
        const auto readout = g.readoutComponentForTest().getBounds();
        for (const juce::Component* button : std::initializer_list<const juce::Component*>{
                 &g.genButtonForTest(), &g.minusButtonForTest(), &g.plusButtonForTest(), &g.measButtonForTest(),
                 &g.ownMinusButtonForTest(), &g.ownPlusButtonForTest()}) {
            CHECK(button->isVisible());
            CHECK(button->getHeight() >= az::ui::touchTarget);
            CHECK(button->getX() >= 300);  // right of the wordmark
            CHECK(button->getBottom() <= az::ui::transportHeight);
            CHECK(button->getRight() <= readout.getX());  // nothing overlaps the readout
        }
        CHECK(readout.getRight() <= c.getWidth());
        const auto* label = dynamic_cast<const juce::Label*>(&g.readoutComponentForTest());
        REQUIRE(label != nullptr);
        const juce::String longest = "MEAS: not while ALIGN runs (applies to the next press)";
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(label->getFont(), longest, 0.0f, 0.0f);
        const float width = glyphs.getBoundingBox(0, -1, false).getWidth();
        const int linesNeeded = static_cast<int>(std::ceil(width / static_cast<float>(readout.getWidth())));
        const int linesFit = static_cast<int>(static_cast<float>(readout.getHeight()) / label->getFont().getHeight());
        INFO("longest readout " << width << " px in " << readout.getWidth() << " x " << readout.getHeight());
        CHECK(linesNeeded <= linesFit);
    }
}

TEST_CASE("T14: a MEAS toggle during a CAL capture is allowed -- CAL plays nothing and reads no drive", kTag) {
    MainComponent c;
    startSynthetic(c);
    Access::calibrationStartClickedForTest(c);
    REQUIRE(Access::calibrationArmedForTest(c));
    genOf(c).measurementModeClicked();
    CHECK(genOf(c).measurementModeForTest() == MeasurementLevelMode::Own);
    CHECK_FALSE(genOf(c).readoutForTest().startsWith("MEAS: not while"));
}
