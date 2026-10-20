// SPDX-License-Identifier: AGPL-3.0-or-later
// ALIGN-UI wave B, PR #72 verifier round 1: what the bound IRs make of the cycle
// integer once the named topology's peak lag is out of it and the guard is in --
// an LR-8 pair that resolves, and a box with extra filtering that is resolved
// right or asked, never a full cycle wrong. Harness: AlignIrFixture.h.
#include "AlignIrFixture.h"

#include "measure/AlignReadout.h"

#include <string>

namespace {
using namespace alignir;
using rta::measure::Source;
using rta::measure::WizardStep;
constexpr const char* kTag = "[main_component_align_ir_cycle]";
}  // namespace

// An LR-8 pair, where the low-pass peaks 9.4 ms (about 450 samples) late and the
// uncorrected arrival difference would sit most of a candidate spacing away: the
// peak-lag correction is what lets the IRs choose the right cycle.
TEST_CASE("align IR (T11): an LR-8 pair resolves to the right cycle from the IRs", kTag) {
    const rta::measure::Topology lr8{ rta::measure::CrossoverFamily::LinkwitzRiley, 8 };
    MainComponent c;
    auto config = fixtureConfig();
    config.measurementFilter = rta::measure::topologyCascade(lr8, true, 100.0, kFs);
    config.secondOutputFilter = rta::measure::topologyCascade(lr8, false, 100.0, kFs);
    prepare(c, config, /*topologyId=*/3);
    sweepOf(c).setDurationsForTest(10.0, 3.0);
    auto& pane = alignPane(c);
    sweepThrough(c, pane.irPanelForTest().sweepMainButtonForTest());
    sweepThrough(c, pane.irPanelForTest().sweepSubButtonForTest());
    REQUIRE(runnerOf(c).boundIr(Source::Main) != nullptr);
    REQUIRE(runnerOf(c).boundIr(Source::Sub) != nullptr);
    const long raw = runnerOf(c).boundIr(Source::Sub)->arrivalSamples - runnerOf(c).boundIr(Source::Main)->arrivalSamples;
    REQUIRE(tickUntil(c, [&] { return pane.measureButtonForTest().isEnabled(); }, 20000));
    pane.measureButtonForTest().onClick();
    REQUIRE(tickUntil(c, [&] { return !runnerOf(c).busy(); }, 40000));
    WARN("LR-8: raw arrival difference " << raw << ", used " << (runnerOf(c).cycleSamples() ? *runnerOf(c).cycleSamples() : -99999)
                                         << ", readout: " << pane.readoutForTest().replace("\n", " | "));
    CHECK(runnerOf(c).step() == WizardStep::Computed);
    CHECK(runnerOf(c).cycleFromIr());
    CHECK(pane.readoutForTest().contains("delay MAIN by 2."));
    CHECK_FALSE(pane.readoutForTest().contains("another candidate"));
}

// The PR #72 verifier's refutation: a sub with ANOTHER filter in its path (a
// subsonic high-pass), so the named LR-4 low-pass lag no longer explains the
// whole arrival. Whatever the verdict, it must never be a full cycle wrong: a
// resolved delay is the same cycle as the fit's best candidate tau* (within half
// a millisecond; the next cycle is ~10.8 ms away), otherwise the run asks.
TEST_CASE("align IR (T11): a box with extra filtering is resolved right or asked, never a wrong cycle", kTag) {
    for (const long subDelay : { 100L, 16L }) {
        CAPTURE(subDelay);
        MainComponent c;
        auto config = fixtureConfig();
        config.secondOutputDelaySamples = static_cast<int>(subDelay);
        auto sections = rta::measure::lr4LowPass(100.0, kFs);
        for (const auto& s : rta::measure::lr4HighPass(40.0, kFs)) sections.push_back(s);
        config.secondOutputFilter = sections;
        prepare(c, config);
        // The shipped sweep: the 2 s test sweep makes the 40 Hz high-pass's IR (a long
        // tail whose peak the short window cannot place) refuse with ArrivalOutOfWindow.
        sweepOf(c).setDurationsForTest(10.0, 3.0);
        auto& pane = alignPane(c);
        sweepThrough(c, pane.irPanelForTest().sweepMainButtonForTest());
        sweepThrough(c, pane.irPanelForTest().sweepSubButtonForTest());
        INFO("sweep readout: " << sweepOf(c).readoutForTest());
        REQUIRE(runnerOf(c).boundIr(Source::Main) != nullptr);
        REQUIRE(runnerOf(c).boundIr(Source::Sub) != nullptr);
        REQUIRE(tickUntil(c, [&] { return pane.measureButtonForTest().isEnabled(); }, 20000));
        pane.measureButtonForTest().onClick();
        REQUIRE(tickUntil(c, [&] { return !runnerOf(c).busy(); }, 40000));
        const double pureMs = 1000.0 * static_cast<double>(subDelay - 4) / kFs;
        WARN("probe sub delay " << subDelay << ": step " << static_cast<int>(runnerOf(c).step()) << ", hint "
             << (runnerOf(c).irHintSamples() ? *runnerOf(c).irHintSamples() : -99999) << " samples, pure " << pureMs
             << " ms, readout: " << pane.readoutForTest().replace("\n", " | "));
        REQUIRE(runnerOf(c).verdict().has_value());
        if (runnerOf(c).step() == WizardStep::Computed) {
            const double tauMs = 1000.0 * rta::measure::resolveCycle(*runnerOf(c).verdict(), runnerOf(c).cycleSamples(), kFs).tauSeconds;
            // The reference is the fit's own best candidate, NOT the pure delay: the
            // extra 40 Hz high-pass adds its group delay inside the 50-200 Hz fit
            // band (tau* reads about 3.8 ms here for 2 ms of delay), and that is
            // part of what the phase measures. A wrong cycle is ~10.8 ms away.
            CHECK(std::fabs(tauMs - 1000.0 * runnerOf(c).verdict()->tauSeconds) < 0.5);
        } else {
            CHECK(runnerOf(c).step() == WizardStep::AskingCycle);
            CHECK(pane.readoutForTest().contains("IRs suggest about"));
        }
    }
}

// PR #72 round 2: the SHIPPED SYNTHETIC defaults (a 40 Hz - 16 kHz band-pass on
// MAIN, an LR-4 low-pass at 100 Hz on SUB) cross acoustically at about 58 Hz,
// not at the 100 Hz corner, and an LR-4 low-pass lags 0.46/fc: 380 samples at the
// crossing against the real 221. Without a seed the integer is only a hint (the
// run asks). H5 closeout D9: a 100 Hz seed, which was meant to let the same IRs
// resolve it (the lag belongs to the processor corner), is 0.79 octave from the
// 58 Hz crossing -- past the third of an octave the IRs are trusted for -- so the
// run still asks, now saying why and offering BOTH integers.
TEST_CASE("align IR (T11): on the shipped defaults the run asks, without a seed and with a 100 Hz one", kTag) {
    MainComponent c;
    c.setSyntheticMode(true);  // the shipped defaults: no restartSyntheticForTest
    sweepOf(c).setDurationsForTest(10.0, 3.0);
    c.selectPaneView(rta::view::PaneSelectorButton::Align);
    auto& pane = alignPane(c);
    pane.highPassComboForTest().setSelectedId(1, juce::sendNotificationSync);
    pane.topologyComboForTest().setSelectedId(2, juce::sendNotificationSync);   // LR-4
    pane.inversionComboForTest().setSelectedId(1, juce::sendNotificationSync);
    pane.seedComboForTest().setSelectedId(1, juce::sendNotificationSync);       // NO SEED
    sweepThrough(c, pane.irPanelForTest().sweepMainButtonForTest());
    sweepThrough(c, pane.irPanelForTest().sweepSubButtonForTest());
    REQUIRE(runnerOf(c).boundIr(Source::Main) != nullptr);
    REQUIRE(runnerOf(c).boundIr(Source::Sub) != nullptr);

    REQUIRE(tickUntil(c, [&] { return pane.measureButtonForTest().isEnabled(); }, 20000));
    pane.measureButtonForTest().onClick();
    REQUIRE(tickUntil(c, [&] { return !runnerOf(c).busy(); }, 40000));
    WARN("no seed: step " << static_cast<int>(runnerOf(c).step()) << ", crossover " << runnerOf(c).verdict()->crossoverHz
         << " Hz, hint " << (runnerOf(c).irHintSamples() ? *runnerOf(c).irHintSamples() : -99999));
    CHECK(runnerOf(c).step() == WizardStep::AskingCycle);
    CHECK(runnerOf(c).irHintSamples().has_value());
    CHECK(pane.readoutForTest().contains("IRs suggest about"));

    pane.seedComboForTest().setSelectedId(4, juce::sendNotificationSync);  // SEED 100 HZ
    REQUIRE(tickUntil(c, [&] { return pane.measureButtonForTest().isEnabled(); }, 20000));
    pane.measureButtonForTest().onClick();
    REQUIRE(tickUntil(c, [&] { return !runnerOf(c).busy(); }, 40000));
    WARN("seed 100: step " << static_cast<int>(runnerOf(c).step()) << ", crossover " << runnerOf(c).verdict()->crossoverHz
         << " Hz, hint " << (runnerOf(c).irHintSamples() ? *runnerOf(c).irHintSamples() : -99999)
         << ", readout: " << pane.readoutForTest().replace("\n", " | "));
    CHECK(runnerOf(c).step() == WizardStep::AskingCycle);
    CHECK_FALSE(runnerOf(c).cycleFromIr());
    REQUIRE(runnerOf(c).seedDisagreement().has_value());
    CHECK(runnerOf(c).seedDisagreement()->seedHz == 100.0);
    CHECK(runnerOf(c).seedDisagreement()->hintAtSeedSamples != runnerOf(c).seedDisagreement()->hintAtCrossingSamples);
    CHECK(pane.readoutForTest().contains("seed and measured crossing disagree (100 Hz vs "));
    CHECK(pane.readoutForTest().contains("with the lag at the seed"));
}
