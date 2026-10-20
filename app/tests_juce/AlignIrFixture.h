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
#include "measure/CycleArrival.h"
#include "measure/SyntheticCrossover.h"
#include "view/AlignPaneView.h"

#include "rta/dsp/Biquad.h"

#include <juce_core/juce_core.h>

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
    Access::restartSyntheticForTest(c, config);
    sweepOf(c).setDurationsForTest(2.0, 0.5);
    c.selectPaneView(rta::view::PaneSelectorButton::Align);
    auto& pane = alignPane(c);
    pane.highPassComboForTest().setSelectedId(1, juce::sendNotificationSync);   // MAIN is high-pass
    pane.topologyComboForTest().setSelectedId(topologyId, juce::sendNotificationSync);
    pane.inversionComboForTest().setSelectedId(1, juce::sendNotificationSync);  // NOT INVERTED
    pane.seedComboForTest().setSelectedId(1, juce::sendNotificationSync);       // NO SEED
}

/// A SWEEP button's own handler, then the sweep to completion.
inline void sweepThrough(MainComponent& c, juce::Button& button) {
    REQUIRE(tickUntil(c, [&] { return button.isEnabled(); }, 20000));
    button.onClick();
    REQUIRE(sweepOf(c).busy());
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 60000));
}

}  // namespace alignir
