// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Shared by test_main_component_align_ir.cpp (SWEEP on a named output, the
// binding, the busy predicates) and test_main_component_align_ir_cycle.cpp (what
// the bound IRs make of the cycle integer). A real MainComponent in SYNTHETIC
// with the wave-A fixture (output 0 an LR4 high-pass at 100 Hz after 4 samples,
// output 1 an LR4 low-pass at 100 Hz after 100), driven through the ALIGN pane's
// own buttons and pickers. The 2 Hz timer is not pumped offscreen: `tickUntil`
// drives it and the pane's own timer by hand, bounded.
//
// EXPECTED ARRIVALS are computed here, not read back: each is the path's delay
// plus the argmax of |h| of the SAME biquad cascade the loopback builds, read off
// an impulse (a closed-form identity of the linear path, not "whatever the code
// printed"). Deconvolution is linear, so the measured IR is that h shifted by the
// delay; its peak can sit one bin off h's own when h is flat-topped.
#pragma once

#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "dev-support/SyntheticCrossoverHp.h"
#include "measure/AlignReadout.h"
#include "measure/CycleArrival.h"
#include "measure/SyntheticCrossover.h"
#include "view/AlignPaneView.h"

#include "rta/dsp/Biquad.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace alignir {

using Access = MainComponentTestAccess;
inline constexpr double kFs = 48000.0;

inline rta::view::AlignPaneView& alignPane(MainComponent& c) {
    auto* pane = dynamic_cast<rta::view::AlignPaneView*>(const_cast<juce::Component*>(&Access::pane(c)));
    REQUIRE(pane != nullptr);
    return *pane;
}
inline rta::measure::AlignRunner& runnerOf(MainComponent& c) { return Access::alignForTest(c).runnerForTest(); }
inline MainComponentSweep& sweepOf(MainComponent& c) { return Access::sweepForTest(c); }

// A sweep is played in REAL time by the loopback (13 s for the shipped 10 s + 3 s
// tail), so how long a test may wait is a function of the sweep it asked for, not
// a constant that load can blow: budget = kSlack x (sweep + tail + 1 s pre-roll),
// floored at 60 s. kSlack = 6 is the headroom for a 100 %-loaded box (the wall
// clock paces the loopback, the analysis shares the cores); unloaded the wait is
// about 1.1 x nominal. An earlier fixed 60 s lost to load on a case that
// legitimately runs 72 s end to end (two boxes x two sweeps).
inline constexpr double kBudgetSlack = 6.0;
inline double gSweepSeconds = 2.0;
inline double gTailSeconds = 0.5;
inline void setSweepDurations(MainComponent& c, double sweepSeconds, double tailSeconds) {
    gSweepSeconds = sweepSeconds;
    gTailSeconds = tailSeconds;
    sweepOf(c).setDurationsForTest(sweepSeconds, tailSeconds);
    // The sweep's own capture timeout (length + 5 s slack) runs on the wall clock
    // while the loopback is paced by it too, so a saturated box aborts a healthy
    // sweep ("no capture -- no REF on route 1?"). Stretch that slack by the same
    // factor as the budgets above. The 100 ms settle is NOT stretched: the capture
    // is armed at the click and the analysis window is sized for that settle, so a
    // longer one moves the arrival out of it ("the arrival does not fit").
    sweepOf(c).setTimeoutSlackForTest(5000.0 * kBudgetSlack);
}
/// MEASURE: two steps, each a settle (0.5 s) plus the dwell (transferFifoDepth *
/// fftSize / fs = 1.365 s at the defaults) in real time, plus a 1 s allowance for
/// the fit; the same slack; floored at 40 s (the former constant).
inline int measureBudgetMs() {
    constexpr double kDwellSeconds = 1.365;
    return std::max(40000, static_cast<int>(1000.0 * kBudgetSlack * (2.0 * (0.5 + kDwellSeconds) + 1.0)));
}
inline int sweepBudgetMs() {
    return std::max(60000, static_cast<int>(1000.0 * kBudgetSlack * (gSweepSeconds + gTailSeconds + 1.0)));
}

template <typename Predicate>
bool tickUntil(MainComponent& c, Predicate&& done, int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        Access::timerTickForTest(c);
        if (c.currentPaneView() == rta::view::PaneView::Align) alignPane(c).tickForTest();
        if (done()) return true;
        juce::Thread::sleep(5);
    }
    Access::timerTickForTest(c);
    return done();
}

inline rta::measure::SyntheticInput::Config fixtureConfig() {
    rta::measure::SyntheticInput::Config config;
    config.sampleRate = kFs;
    config.measurementDelaySamples = 4;
    config.measurementFilter = rta::measure::lr4HighPass(100.0, kFs);
    config.secondOutputDelaySamples = 100;
    config.secondOutputFilter = rta::measure::lr4LowPass(100.0, kFs);
    return config;
}

/// delay + argmax|h| of a cascade, h read off a unit impulse.
inline long expectedArrival(const std::vector<rta::dsp::Biquad::Coeffs>& sections, long delay) {
    rta::dsp::BiquadCascade cascade(sections);
    double best = 0.0;
    long bestIndex = 0;
    for (long n = 0; n < 4096; ++n) {
        const double y = cascade.processSample(n == 0 ? 1.0 : 0.0);
        if (std::fabs(y) > best) {
            best = std::fabs(y);
            bestIndex = n;
        }
    }
    return delay + bestIndex;
}
/// The named topology's low-pass peak lag less its high-pass lag at `fc` (LR-4).
inline long lagAt(double fc) {
    const rta::measure::Topology lr4{ rta::measure::CrossoverFamily::LinkwitzRiley, 4 };
    return std::lround(rta::measure::topologyPeakLagSamples(lr4, false, fc, kFs) -
                       rta::measure::topologyPeakLagSamples(lr4, true, fc, kFs));
}
inline long expectedMainArrival() { return expectedArrival(rta::measure::lr4HighPass(100.0, kFs), 4); }
inline long expectedSubArrival() { return expectedArrival(rta::measure::lr4LowPass(100.0, kFs), 100); }

/// SYNTHETIC with `config`, the ALIGN pane up, the four questions answered
/// through the combos (topologyId 2 = LR-4, 3 = LR-8), the test sweep (2 s).
inline void prepare(MainComponent& c, rta::measure::SyntheticInput::Config config = fixtureConfig(),
                    int topologyId = 2) {
    c.setSyntheticMode(true);
    // H7 T13: measurements follow GEN's level by default (-30.0); this fixture was settled at the
    // record's -12 dBFS, so it pins GEN there and keeps testing what it tested.
    MainComponentTestAccess::pinGeneratorLevelForTest(c, -12.0);
    Access::restartSyntheticForTest(c, config);
    setSweepDurations(c, 2.0, 0.5);
    c.selectPaneView(rta::view::PaneSelectorButton::Align);
    auto& pane = alignPane(c);
    pane.highPassComboForTest().setSelectedId(1, juce::sendNotificationSync);   // MAIN is high-pass
    pane.topologyComboForTest().setSelectedId(topologyId, juce::sendNotificationSync);
    pane.inversionComboForTest().setSelectedId(1, juce::sendNotificationSync);  // NOT INVERTED
    pane.seedComboForTest().setSelectedId(1, juce::sendNotificationSync);       // NO SEED
}

/// A SWEEP button's own handler, then the sweep to completion.
inline void sweepThrough(MainComponent& c, juce::Button& button) {
    // A press while the previous excitation is still ramping out is refused ("the output is
    // still playing a source"); under load the ramp (paced by the wall clock) outlasts the
    // finished sweep, so wait for the engine itself to report quiescence.
    REQUIRE(tickUntil(c, [&] { return button.isEnabled() && Access::outputSourceQuiescentForTest(c); }, 20000));
    button.onClick();
    INFO("sweep readout: " << sweepOf(c).readoutForTest());
    REQUIRE(sweepOf(c).busy());
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, sweepBudgetMs()));
    UNSCOPED_INFO("sweep finished: " << sweepOf(c).readoutForTest());
}

/// The readout's delay is the fit candidate the cycle integer chose. Its VALUE
/// carries the estimator's own tau* bias and jitter (wave A measured +0.05..0.24
/// ms on an LR-4 pair, more on a steeper LR-8 phase: CI read 3.01 ms for 2 ms),
/// so a fixed-decimal string ("delay MAIN by 2.") is the floor of one grid. What
/// the test can honestly claim: the chosen candidate is the fit's own tau* (the
/// right cycle), and it lies within HALF the candidate spacing of the ideal delay,
/// which is the discrimination bound against the neighbouring cycle. The spacing
/// is read from the candidate list (smallest gap), never typed. `direction` text
/// stays asserted. Prints the actual tau, delta and crossing.
inline void checkChosenCycleNearIdeal(MainComponent& c, double idealMs, const char* note) {
    const auto& verdict = *runnerOf(c).verdict();
    const auto resolved = rta::measure::resolveCycle(verdict, runnerOf(c).cycleSamples(), kFs);
    auto sorted = verdict.candidateTausSeconds;
    std::sort(sorted.begin(), sorted.end());
    double spacing = 1.0 / verdict.crossoverHz;  // one candidate only: fall back to a cycle at the crossing
    for (std::size_t i = 1; i < sorted.size(); ++i) spacing = std::min(spacing, sorted[i] - sorted[i - 1]);
    const double tauMs = 1000.0 * resolved.tauSeconds;
    WARN(note << ": chosen tau " << tauMs << " ms (tau* " << 1000.0 * verdict.tauSeconds << "), delta "
              << verdict.deltaRadians * 180.0 / 3.14159265358979323846 << " deg, crossing " << verdict.crossoverHz
              << " Hz, spacing " << 1000.0 * spacing << " ms, ideal " << idealMs << " ms");
    CHECK(std::fabs(resolved.tauSeconds - verdict.tauSeconds) < 1.0e-9);  // the integer chose tau*'s own cycle
    CHECK(std::fabs(tauMs - idealMs) < 1000.0 * spacing / 2.0);
    CHECK(alignPane(c).readoutForTest().contains("delay MAIN by "));
}

}  // namespace alignir
