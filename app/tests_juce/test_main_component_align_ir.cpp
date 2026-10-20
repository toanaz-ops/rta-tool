// SPDX-License-Identifier: AGPL-3.0-or-later
// ALIGN-UI lane wave B, tasks T10 and T11 (docs/plans/2026-10-20-align-ui-lane-plan.md):
// ALIGN-UI lane wave B, tasks T10 and T11 (docs/plans/2026-10-20-align-ui-lane-plan.md):
// SWEEP on a NAMED output, the IR binding, and the busy predicates. Shared harness
// and the fixture description: AlignIrFixture.h. What the bound IRs make of the
// cycle integer is test_main_component_align_ir_cycle.cpp.
#include "AlignIrFixture.h"

#include "measure/AlignReadout.h"

#include <string>

namespace {
using namespace alignir;
using rta::measure::Source;
using rta::measure::WizardStep;
constexpr const char* kTag = "[main_component_align_ir]";
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
    // DERIVED bound (replaces a +-3 that was the floor of one grid). The arrivals
    // are identical every run (main 4, sub 320); what moves is lag = lagAt(crossing),
    // because SyntheticInput paces by wall clock and the snapshot is taken from the
    // message-thread timer, so WHICH blocks of the fixed-seed (0x5EEDu) pink stream
    // are averaged varies, and the crossing with them. Crossing resolution: the
    // spectral crossover interpolates between bins of 48000/4096 = 11.7 Hz, so its
    // reading cannot be off by more than half a bin, 5.9 Hz; with the fixture's
    // corner at 100 Hz we allow dF = 5 Hz (measured jitter about +-1.5 Hz). The LR4
    // lag is about 0.46 fs / f (2.2 samples per Hz here), so the integer may move
    // over [raw - lagAt(100 - dF), raw - lagAt(100 + dF)] and nowhere else.
    constexpr double kCornerHz = 100.0;
    constexpr double kCrossingToleranceHz = 5.0;
    const double crossingHz = runnerOf(c).verdict()->crossoverHz;
    CHECK(std::fabs(crossingHz - kCornerHz) <= kCrossingToleranceHz);
    const long raw = subArrival - mainArrival;
    CHECK(*runnerOf(c).cycleSamples() >= raw - lagAt(kCornerHz - kCrossingToleranceHz));
    CHECK(*runnerOf(c).cycleSamples() <= raw - lagAt(kCornerHz + kCrossingToleranceHz));
    WARN("T11 crossing " << crossingHz << " Hz, allowed integer range [" << raw - lagAt(kCornerHz - kCrossingToleranceHz)
                         << ", " << raw - lagAt(kCornerHz + kCrossingToleranceHz) << "]");
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
    checkChosenCycleNearIdeal(c, 2.0, "T11 IR integer");
    CHECK_FALSE(pane.readoutForTest().contains("your offset"));

    // A typed offset overrides the IR integer, and the status says whose it is.
    pane.offsetEditorForTest().setText("2.0", juce::dontSendNotification);
    pane.offsetButtonForTest().onClick();
    CHECK(*runnerOf(c).cycleSamples() == 96);
    CHECK_FALSE(runnerOf(c).cycleFromIr());
    CHECK(runnerOf(c).status().find("your typed offset") != std::string::npos);
    checkChosenCycleNearIdeal(c, 2.0, "T11 typed 2.0 ms");

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
