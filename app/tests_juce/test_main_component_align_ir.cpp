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
#include "measure/AlignReadout.h"
#include "measure/CycleArrival.h"
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
/// The named topology's low-pass peak lag less its high-pass lag at `fc` (LR-4).
long lagAt(double fc) {
    const rta::measure::Topology lr4{ rta::measure::CrossoverFamily::LinkwitzRiley, 4 };
    return std::lround(rta::measure::topologyPeakLagSamples(lr4, false, fc, kFs) -
                       rta::measure::topologyPeakLagSamples(lr4, true, fc, kFs));
}
long expectedMainArrival() { return expectedArrival(rta::measure::lr4HighPass(100.0, kFs), 4); }
long expectedSubArrival() { return expectedArrival(rta::measure::lr4LowPass(100.0, kFs), 100); }

void prepare(MainComponent& c, rta::measure::SyntheticInput::Config config = fixtureConfig(), int topologyId = 2) {
    c.setSyntheticMode(true);
    Access::restartSyntheticForTest(c, config);
    sweepOf(c).setDurationsForTest(2.0, 0.5);
    c.selectPaneView(rta::view::PaneSelectorButton::Align);
    auto& pane = alignPane(c);
    pane.highPassComboForTest().setSelectedId(1, juce::sendNotificationSync);   // MAIN is high-pass
    pane.topologyComboForTest().setSelectedId(topologyId, juce::sendNotificationSync);  // 2 = LR-4, 3 = LR-8
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
    // REGRESSION LOCK, not an independent check: the pipeline picks its own peak
    // from these same samples, so this restates its definition. It guards the
    // field's meaning (peak minus relabelled origin) against a refactor; the
    // closed-form bound above is the evidence.
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
    // The integer is the arrival difference LESS the LR-4 peak lag at the
    // measured crossover: 316 raw, about 96 once the low-pass's ~221-sample lag is out.
    const long lag = lagAt(runnerOf(c).verdict()->crossoverHz);
    CHECK(*runnerOf(c).cycleSamples() == subArrival - mainArrival - lag);
    WARN("T11 integer " << *runnerOf(c).cycleSamples() << " (raw " << subArrival - mainArrival << ", lag " << lag
                        << "), the fixture's pure delay is 96");
    CHECK(std::llabs(static_cast<long long>(*runnerOf(c).cycleSamples()) - 96) <= 3);
    CHECK(pane.offsetButtonForTest().isEnabled());  // a typed offset may always override

    // The signals are listed, with their reasons; the sub's findPolarity is a refusal.
    const auto lines = runnerOf(c).polaritySignalLines();
    std::string joined;
    for (const auto& line : lines) joined += line + "\n";
    INFO(joined);
    CHECK(joined.find("rho: ") != std::string::npos);
    CHECK(joined.find("findPolarity, low-pass side: refused (BandTooHigh)") != std::string::npos);
    CHECK(pane.irInfoForTest().contains("findPolarity, low-pass side: refused (BandTooHigh)"));
    // The verdict resolves the integer: candidates sit ~10.8 ms apart, the
    // corrected integer is within ~0.1 ms of the right one.
    CHECK(pane.readoutForTest().contains("delay MAIN by 2."));
    CHECK_FALSE(pane.readoutForTest().contains("your offset"));

    // A typed offset overrides the IR integer, and the status says whose it is.
    pane.offsetEditorForTest().setText("2.0", juce::dontSendNotification);
    pane.offsetButtonForTest().onClick();
    CHECK(*runnerOf(c).cycleSamples() == 96);
    CHECK_FALSE(runnerOf(c).cycleFromIr());
    CHECK(runnerOf(c).status().find("your typed offset") != std::string::npos);
    CHECK(pane.readoutForTest().contains("delay MAIN by 2."));

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
    // The crossover is estimated afresh from pink noise each run, so its lag may differ by a sample or two.
    CHECK(*runnerOf(c).cycleSamples() == subArrival - mainArrival - lagAt(runnerOf(c).verdict()->crossoverHz));
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
    Access::alignForTest(c).sweepOnSide(Source::Sub);  // refused: must not forget MAIN's pending bind
    Access::timerTickForTest(c);
    pane.tickForTest();
    CHECK_FALSE(pane.measureButtonForTest().isEnabled());
    CHECK_FALSE(panel.sweepMainButtonForTest().isEnabled());
    CHECK_FALSE(panel.sweepSubButtonForTest().isEnabled());
    CHECK_FALSE(runnerOf(c).press());
    CHECK_FALSE(runnerOf(c).busy());
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 60000));
    CHECK(runnerOf(c).boundIr(Source::Main) != nullptr);  // the running sweep still bound itself
    CHECK(runnerOf(c).boundIr(Source::Sub) == nullptr);

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

TEST_CASE("align IR (T11): a result from another output does not bind", kTag) {
    MainComponent c;
    prepare(c);
    auto& panel = alignPane(c).irPanelForTest();
    sweepThrough(c, panel.sweepMainButtonForTest());
    const std::string firstId = runnerOf(c).boundIrId(Source::Main);
    REQUIRE_FALSE(firstId.empty());

    // SWEEP SUB is running on output 2; a `swept` report naming output 1 is not its result.
    REQUIRE(tickUntil(c, [&] { return panel.sweepSubButtonForTest().isEnabled(); }, 20000));
    panel.sweepSubButtonForTest().onClick();
    REQUIRE(sweepOf(c).busy());
    Access::alignForTest(c).onSwept(firstId, 0);
    CHECK(runnerOf(c).boundIr(Source::Sub) == nullptr);
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 60000));
    CHECK(runnerOf(c).boundIr(Source::Sub) != nullptr);  // its own result still binds
    CHECK(runnerOf(c).boundIrId(Source::Sub) != firstId);
}

TEST_CASE("align IR (T11): an IR measured at another sample rate is ignored, and the run says so", kTag) {
    MainComponent c;
    prepare(c);
    const auto fake = [](long arrival) {
        auto ir = std::make_shared<rta::measure::SweepIr>();
        ir->samples.assign(4096, 0.0f);
        ir->samples[static_cast<std::size_t>(100 + arrival)] = 1.0f;
        ir->originIndex = 100;
        ir->arrivalSamples = arrival;
        ir->sampleRate = 44100.0;  // the SYNTHETIC output runs at 48 kHz
        return ir;
    };
    runnerOf(c).bindIr(Source::Main, "fake-main", fake(40));
    runnerOf(c).bindIr(Source::Sub, "fake-sub", fake(64));
    auto& pane = alignPane(c);
    REQUIRE(tickUntil(c, [&] { return pane.measureButtonForTest().isEnabled(); }, 20000));
    pane.measureButtonForTest().onClick();
    REQUIRE(runnerOf(c).busy());
    CHECK(runnerOf(c).status().find("IR MAIN ignored: its sample rate differs") != std::string::npos);
    CHECK(runnerOf(c).status().find("IR SUB ignored") != std::string::npos);
    REQUIRE(tickUntil(c, [&] { return !runnerOf(c).busy(); }, 40000));
    CHECK(runnerOf(c).step() == WizardStep::AskingCycle);  // nothing was supplied
    CHECK_FALSE(runnerOf(c).irHintSamples().has_value());
}

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
