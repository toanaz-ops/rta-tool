// SPDX-License-Identifier: AGPL-3.0-or-later
// ALIGN-UI lane wave B, tasks T10 and T11 (docs/plans/2026-10-20-align-ui-lane-plan.md):
// SWEEP on a NAMED output, and the IR binding that lets the wizard take its
// cycle integer from two arrivals. A real MainComponent in SYNTHETIC with the
// wave-A fixture (output 0 an LR4 high-pass at 100 Hz after 4 samples, output 1
// an LR4 low-pass at 100 Hz after 100), driven through the ALIGN pane's own
// buttons and pickers. The 2 Hz timer is not pumped offscreen: `tickUntil`
// drives it and the pane's own timer by hand, bounded.
//
// EXPECTED ARRIVALS are computed here, not read back: each is the path's delay
// plus the argmax of |h| of the SAME biquad cascade the loopback builds, read off
// an impulse (a closed-form identity of the linear path, not "whatever the code
// printed"). Deconvolution is linear, so the measured IR is that h shifted by the
// delay; its peak can sit one bin off h's own when h is flat-topped, which the
// numbers printed below show whether it did.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "dev-support/SyntheticCrossoverHp.h"
#include "measure/SyntheticCrossover.h"
#include "view/AlignPaneView.h"

#include "rta/dsp/Biquad.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

using Access = MainComponentTestAccess;
using rta::measure::Source;
using rta::measure::WizardStep;
constexpr const char* kTag = "[main_component_align_ir]";
constexpr double kFs = 48000.0;

rta::view::AlignPaneView& alignPane(MainComponent& c) {
    auto* pane = dynamic_cast<rta::view::AlignPaneView*>(const_cast<juce::Component*>(&Access::pane(c)));
    REQUIRE(pane != nullptr);
    return *pane;
}
rta::measure::AlignRunner& runnerOf(MainComponent& c) { return Access::alignForTest(c).runnerForTest(); }
MainComponentSweep& sweepOf(MainComponent& c) { return Access::sweepForTest(c); }

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

rta::measure::SyntheticInput::Config fixtureConfig() {
    rta::measure::SyntheticInput::Config config;
    config.sampleRate = kFs;
    config.measurementDelaySamples = 4;
    config.measurementFilter = rta::measure::lr4HighPass(100.0, kFs);
    config.secondOutputDelaySamples = 100;
    config.secondOutputFilter = rta::measure::lr4LowPass(100.0, kFs);
    return config;
}

/// delay + argmax|h| of a cascade, h read off a unit impulse.
long expectedArrival(const std::vector<rta::dsp::Biquad::Coeffs>& sections, long delay) {
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
long expectedMainArrival() { return expectedArrival(rta::measure::lr4HighPass(100.0, kFs), 4); }
long expectedSubArrival() { return expectedArrival(rta::measure::lr4LowPass(100.0, kFs), 100); }

void prepare(MainComponent& c) {
    c.setSyntheticMode(true);
    Access::restartSyntheticForTest(c, fixtureConfig());
    sweepOf(c).setDurationsForTest(2.0, 0.5);
    c.selectPaneView(rta::view::PaneSelectorButton::Align);
    auto& pane = alignPane(c);
    pane.highPassComboForTest().setSelectedId(1, juce::sendNotificationSync);   // MAIN is high-pass
    pane.topologyComboForTest().setSelectedId(2, juce::sendNotificationSync);   // LR-4
    pane.inversionComboForTest().setSelectedId(1, juce::sendNotificationSync);  // NOT INVERTED
    pane.seedComboForTest().setSelectedId(1, juce::sendNotificationSync);       // NO SEED
}

/// A SWEEP button's own handler, then the sweep to completion.
void sweepThrough(MainComponent& c, juce::Button& button) {
    REQUIRE(tickUntil(c, [&] { return button.isEnabled(); }, 20000));
    button.onClick();
    REQUIRE(sweepOf(c).busy());
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 60000));
}

}  // namespace

TEST_CASE("align IR (T10): a sweep on output 2 plays on output 2 and is named for it", kTag) {
    MainComponent c;
    prepare(c);
    auto& library = Access::libraryForTest(c);
    REQUIRE(sweepOf(c).clickedOnOutput(1));
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 60000));
    INFO("readout: " << sweepOf(c).readoutForTest());

    REQUIRE(library.entries().size() == 1);
    const auto& entry = library.entries()[0];
    CHECK(entry.name.rfind("SWEEP out2 @ ", 0) == 0);
    CHECK(entry.group == "SWEEP");
    const auto ir = sweepOf(c).irStoreForTest().find(entry.traceId);
    REQUIRE(ir != nullptr);
    const long expected = expectedSubArrival();
    WARN("T10 ch1 sweep arrival " << ir->arrivalSamples << ", expected 100 + argmax|h| = " << expected);
    // Within one sample of the closed form, as the plain SWEEP test already
    // argues: the measured peak is the peak of h convolved with the loop's ~1
    // sample pulse, and an LR4 low-pass at 100 Hz is flat-topped (measured here:
    // one bin below h's own argmax). Output 0's path would read about 4 + its
    // own peak, hundreds of samples away -- the bound discriminates, it does not
    // claim accuracy.
    CHECK(std::labs(ir->arrivalSamples - expected) <= 1);
    CHECK(ir->arrivalSamples > 100);
    // Exact identity of the pipeline itself: the stored samples' own peak, read
    // here independently, sits `arrivalSamples` after the relabelled origin.
    std::size_t peak = 0;
    for (std::size_t i = 0; i < ir->samples.size(); ++i) {
        if (std::fabs(ir->samples[i]) > std::fabs(ir->samples[peak])) peak = i;
    }
    CHECK(static_cast<long>(peak) - static_cast<long>(ir->originIndex) == ir->arrivalSamples);
}

TEST_CASE("align IR (T10): the plain SWEEP button still plays on output 1, keeps its name, binds nothing", kTag) {
    MainComponent c;
    prepare(c);
    auto& library = Access::libraryForTest(c);
    sweepOf(c).clicked();
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 60000));
    REQUIRE(library.entries().size() == 1);
    CHECK(library.entries()[0].name.rfind("SWEEP @ ", 0) == 0);
    CHECK(runnerOf(c).boundIr(Source::Main) == nullptr);  // only an ALIGN button's sweep auto-binds
    CHECK(runnerOf(c).boundIr(Source::Sub) == nullptr);
    const auto ir = sweepOf(c).irStoreForTest().find(library.entries()[0].traceId);
    REQUIRE(ir != nullptr);
    CHECK(ir->arrivalSamples == expectedMainArrival());
}

TEST_CASE("align IR (T11): SWEEP MAIN, SWEEP SUB, MEASURE -- the integer is the arrivals' difference", kTag) {
    MainComponent c;
    prepare(c);
    auto& pane = alignPane(c);
    auto& library = Access::libraryForTest(c);

    sweepThrough(c, pane.irPanelForTest().sweepMainButtonForTest());
    REQUIRE(library.entries().size() == 1);
    CHECK(library.entries()[0].name.rfind("SWEEP out1 @ ", 0) == 0);
    REQUIRE(runnerOf(c).boundIr(Source::Main) != nullptr);  // auto-bound to MAIN
    CHECK(runnerOf(c).boundIrId(Source::Main) == library.entries()[0].traceId);
    CHECK(runnerOf(c).boundIr(Source::Sub) == nullptr);

    sweepThrough(c, pane.irPanelForTest().sweepSubButtonForTest());
    REQUIRE(library.entries().size() == 2);
    CHECK(library.entries()[1].name.rfind("SWEEP out2 @ ", 0) == 0);
    REQUIRE(runnerOf(c).boundIr(Source::Sub) != nullptr);
    CHECK(runnerOf(c).boundIrId(Source::Sub) == library.entries()[1].traceId);
    CHECK(runnerOf(c).boundIrId(Source::Main) == library.entries()[0].traceId);  // SUB's sweep left MAIN's alone

    const long mainArrival = runnerOf(c).boundIr(Source::Main)->arrivalSamples;
    const long subArrival = runnerOf(c).boundIr(Source::Sub)->arrivalSamples;
    WARN("T11 arrivals: main " << mainArrival << " (expected " << expectedMainArrival() << "), sub " << subArrival
                               << " (expected " << expectedSubArrival() << ")");

    // The pane shows both bindings, with their arrivals, and the pickers agree.
    alignPane(c).tickForTest();
    CHECK(pane.irInfoForTest().contains("IR MAIN: SWEEP out1 @ "));
    CHECK(pane.irInfoForTest().contains("IR SUB : SWEEP out2 @ "));
    CHECK(pane.irInfoForTest().contains(" arrival +" + juce::String(mainArrival) + " samples"));
    CHECK(pane.irPanelForTest().mainPickerForTest().getSelectedId() == 3);  // newest first: id 2 is out2, 3 is out1
    CHECK(pane.irPanelForTest().subPickerForTest().getSelectedId() == 2);

    // MEASURE through the button: no coarse offset is asked for.
    REQUIRE(tickUntil(c, [&] { return pane.measureButtonForTest().isEnabled(); }, 20000));
    pane.measureButtonForTest().onClick();
    REQUIRE(runnerOf(c).busy());
    REQUIRE(tickUntil(c, [&] { return !runnerOf(c).busy(); }, 40000));
    INFO("status: " << runnerOf(c).status());
    CHECK(runnerOf(c).step() == WizardStep::Computed);  // not AskingCycle
    CHECK(runnerOf(c).cycleFromIr());
    REQUIRE(runnerOf(c).cycleSamples().has_value());
    CHECK(*runnerOf(c).cycleSamples() == subArrival - mainArrival);
    const long expectedInteger = expectedSubArrival() - expectedMainArrival();
    WARN("T11 integer " << *runnerOf(c).cycleSamples() << ", closed-form " << expectedInteger);
    CHECK(std::llabs(static_cast<long long>(*runnerOf(c).cycleSamples()) - expectedInteger) <= 1);
    CHECK_FALSE(pane.offsetButtonForTest().isEnabled());  // the IRs gave it; the box has nothing to say

    // The signals are listed, with their reasons; the sub's findPolarity is a refusal.
    const auto lines = runnerOf(c).polaritySignalLines();
    std::string joined;
    for (const auto& line : lines) joined += line + "\n";
    INFO(joined);
    CHECK(joined.find("rho: ") != std::string::npos);
    CHECK(joined.find("findPolarity, low-pass side: refused (BandTooHigh)") != std::string::npos);
    CHECK(pane.irInfoForTest().contains("findPolarity, low-pass side: refused (BandTooHigh)"));
    CHECK(pane.readoutForTest().contains("delay "));  // the verdict resolves the integer, offset box untouched

    // The cost of the fit on the message thread (EQ risk-2 precedent: > 100 ms moves to a worker).
    WARN("T11 compute() = " << runnerOf(c).lastComputeMs() << " ms on the message thread");
    CHECK(runnerOf(c).lastComputeMs() > 0.0);
    CHECK(pane.irInfoForTest().contains("fit + polarity: "));

    // Unbinding one through its picker: the next MEASURE has no integer and asks.
    pane.irPanelForTest().mainPickerForTest().setSelectedId(1, juce::sendNotificationSync);
    CHECK(runnerOf(c).boundIr(Source::Main) == nullptr);
    REQUIRE(tickUntil(c, [&] { return pane.measureButtonForTest().isEnabled(); }, 20000));
    pane.measureButtonForTest().onClick();
    REQUIRE(tickUntil(c, [&] { return !runnerOf(c).busy(); }, 40000));
    CHECK(runnerOf(c).step() == WizardStep::AskingCycle);
    CHECK_FALSE(runnerOf(c).cycleFromIr());

    // Re-binding it from the picker brings the integer back.
    pane.irPanelForTest().mainPickerForTest().setSelectedId(3, juce::sendNotificationSync);
    REQUIRE(runnerOf(c).boundIr(Source::Main) != nullptr);
    REQUIRE(tickUntil(c, [&] { return pane.measureButtonForTest().isEnabled(); }, 20000));
    pane.measureButtonForTest().onClick();
    REQUIRE(tickUntil(c, [&] { return !runnerOf(c).busy(); }, 40000));
    CHECK(runnerOf(c).step() == WizardStep::Computed);
    CHECK(*runnerOf(c).cycleSamples() == subArrival - mainArrival);
}

TEST_CASE("align IR (T11): SWEEP MAIN/SUB and MEASURE obey the same busy predicates both ways", kTag) {
    MainComponent c;
    prepare(c);
    auto& pane = alignPane(c);
    auto& panel = pane.irPanelForTest();

    // (1) A sweep in flight: MEASURE and both SWEEP buttons are off.
    REQUIRE(tickUntil(c, [&] { return panel.sweepMainButtonForTest().isEnabled() && pane.measureButtonForTest().isEnabled(); }, 20000));
    panel.sweepMainButtonForTest().onClick();
    REQUIRE(sweepOf(c).busy());
    Access::timerTickForTest(c);
    pane.tickForTest();
    CHECK_FALSE(pane.measureButtonForTest().isEnabled());
    CHECK_FALSE(panel.sweepMainButtonForTest().isEnabled());
    CHECK_FALSE(panel.sweepSubButtonForTest().isEnabled());
    CHECK_FALSE(runnerOf(c).press());
    CHECK_FALSE(runnerOf(c).busy());
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 60000));

    // (2) A run in flight: both SWEEP buttons are off, and a click that reaches
    // the sweep anyway is refused by ITS predicate, adds nothing and binds nothing.
    REQUIRE(tickUntil(c, [&] { return pane.measureButtonForTest().isEnabled(); }, 20000));
    pane.measureButtonForTest().onClick();
    REQUIRE(runnerOf(c).busy());
    pane.tickForTest();
    CHECK_FALSE(panel.sweepMainButtonForTest().isEnabled());
    CHECK_FALSE(panel.sweepSubButtonForTest().isEnabled());
    const auto before = Access::libraryForTest(c).entries().size();
    Access::alignForTest(c).sweepOnSide(Source::Sub);  // what the button handler calls
    CHECK_FALSE(sweepOf(c).busy());
    CHECK(sweepOf(c).readoutForTest().contains("refused"));
    CHECK(Access::libraryForTest(c).entries().size() == before);
    CHECK(runnerOf(c).busy());  // the run was not disturbed
    REQUIRE(tickUntil(c, [&] { return !runnerOf(c).busy(); }, 40000));
}
