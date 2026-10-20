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
