// SPDX-License-Identifier: AGPL-3.0-or-later
// ALIGN-UI lane wave A, task T6 (docs/plans/2026-10-20-align-ui-lane-plan.md):
// the ALIGN pane against a REAL MainComponent in SYNTHETIC mode, through the
// pane's own combos and buttons. The loopback (T0) plays each output through
// its own path, the real analysis thread measures it, the real runner and
// wizard fit it. Nothing here stands in for the state machine.
//
// THE FIXTURE (plan "Device-free acceptance"): noise off; output 0 an LR4
// high-pass at 100 Hz after 4 samples, output 1 an LR4 low-pass at 100 Hz after
// 100, so the low-pass side arrives 96 samples = +2.000 ms later. HP = MAIN,
// LR-4, NOT INVERTED, NO SEED.
//
// Every tolerance is a DISCRIMINATION bound -- half the gap between the right
// answer and the named mutant's -- not an accuracy claim (accuracy is core
// C1's job):
//   swapped solo channels  -> tau about -2 ms        -> |tau - 2 ms| < 2 ms
//   inverted ch 1 path     -> |delta| about 180 deg  -> |delta| < 90 deg
//   SUM routing main only  -> 6.02 dB below predicted -> gap at fc < 3 dB
// The numbers are printed (WARN) beside their bounds. They are not tightened.
//
// The 2 Hz timer is not pumped offscreen; `tickUntil` drives `timerCallback` and
// the pane's own timer by hand, bounded.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "dev-support/SyntheticCrossoverHp.h"
#include "measure/SyntheticCrossover.h"
#include "view/AlignPaneView.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <numbers>
#include <string>
#include <vector>

namespace {

using Access = MainComponentTestAccess;
using rta::measure::AlignBlock;
using rta::measure::AlignOutcome;
using rta::measure::WizardStep;
constexpr const char* kTag = "[main_component_align]";
constexpr double kFs = 48000.0;
constexpr double kPi = std::numbers::pi;

rta::view::AlignPaneView& alignPane(MainComponent& c) {
    auto* pane = dynamic_cast<rta::view::AlignPaneView*>(const_cast<juce::Component*>(&Access::pane(c)));
    REQUIRE(pane != nullptr);
    return *pane;
}

rta::measure::AlignRunner& runnerOf(MainComponent& c) { return Access::alignForTest(c).runnerForTest(); }

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
    // measurementNoiseDb left at its default: genuinely OFF.
    return config;
}

/// SYNTHETIC with the plan's fixture, the ALIGN pane up, the four questions
/// answered through the combos the operator uses.
void prepare(MainComponent& c) {
    c.setSyntheticMode(true);
    Access::restartSyntheticForTest(c, fixtureConfig());
    c.selectPaneView(rta::view::PaneSelectorButton::Align);
    auto& pane = alignPane(c);
    pane.highPassComboForTest().setSelectedId(1, juce::sendNotificationSync);   // MAIN is high-pass
    pane.topologyComboForTest().setSelectedId(2, juce::sendNotificationSync);   // LR-4
    pane.inversionComboForTest().setSelectedId(1, juce::sendNotificationSync);  // NOT INVERTED
    pane.seedComboForTest().setSelectedId(1, juce::sendNotificationSync);       // NO SEED
}

/// MEASURE through the button's own handler, once a TRANSFER is publishing.
void measureToVerdict(MainComponent& c) {
    REQUIRE(tickUntil(c, [&] { return alignPane(c).measureButtonForTest().isEnabled(); }, 20000));
    alignPane(c).measureButtonForTest().onClick();
    REQUIRE(runnerOf(c).busy());
    REQUIRE(tickUntil(c, [&] { return !runnerOf(c).busy(); }, 40000));
}

}  // namespace

TEST_CASE("align: MEASURE, the coarse offset and SUM through the real pane (the plan's fixture)", kTag) {
    MainComponent c;
    prepare(c);
    auto& library = Access::libraryForTest(c);
    REQUIRE(library.entries().empty());

    measureToVerdict(c);
    INFO("status: " << runnerOf(c).status());

    // Two captures, in group ALIGN, named for the output that played.
    REQUIRE(library.entries().size() == 2);
    CHECK(library.entries()[0].name.rfind("ALIGN MAIN @ ", 0) == 0);
    CHECK(library.entries()[1].name.rfind("ALIGN SUB @ ", 0) == 0);
    CHECK(library.entries()[0].group == "ALIGN");
    CHECK(library.entries()[1].group == "ALIGN");
    REQUIRE(runnerOf(c).verdict().has_value());
    const auto& verdict = *runnerOf(c).verdict();
    REQUIRE(verdict.fitRefusal == rta::dsp::CrossoverRefusal::None);
    CHECK(runnerOf(c).step() == WizardStep::AskingCycle);

    const double tauMs = verdict.tauSeconds * 1e3;
    const double deltaDeg = verdict.deltaRadians * 180.0 / kPi;
    WARN("T6 tau = " << tauMs << " ms (bound |tau - 2| < 2), delta = " << deltaDeg
                     << " deg (bound |delta| < 90), R = " << verdict.agreement << ", crossover "
                     << verdict.crossoverHz << " Hz, " << verdict.candidateTausSeconds.size() << " candidates");
    CHECK(std::abs(tauMs - 2.0) < 2.0);
    CHECK(std::abs(deltaDeg) < 90.0);
    CHECK(alignPane(c).readoutForTest().contains("crossover "));
    CHECK_FALSE(alignPane(c).readoutForTest().contains("OPPOSITE polarity"));  // no inversion sentence

    // The coarse offset, typed the way the operator types it.
    auto& pane = alignPane(c);
    REQUIRE(pane.offsetButtonForTest().isEnabled());
    pane.offsetEditorForTest().setText("2.0", juce::dontSendNotification);
    pane.offsetButtonForTest().onClick();
    REQUIRE(runnerOf(c).step() == WizardStep::Computed);
    CHECK(runnerOf(c).cycleSamples() == std::optional<std::ptrdiff_t>(96));
    CHECK(pane.readoutForTest().contains("delay MAIN by "));
    CHECK(pane.surfaceForTest().pointCount() > 0);  // the suggested delay is a pending op on the surface

    // SUM: both outputs together, the dwell, one stored capture.
    REQUIRE(pane.sumButtonForTest().isEnabled());
    pane.sumButtonForTest().onClick();
    REQUIRE(runnerOf(c).step() == WizardStep::MeasuringSum);
    REQUIRE(tickUntil(c, [&] { return !runnerOf(c).busy(); }, 40000));
    REQUIRE(runnerOf(c).outcomeForTest() == AlignOutcome::SumMeasured);
    REQUIRE(library.entries().size() == 3);
    CHECK(library.entries()[2].name.rfind("ALIGN SUM @ ", 0) == 0);
    CHECK(library.entries()[2].group == "ALIGN");

    // The measured sum against the predicted sum, at the crossover bin.
    alignPane(c).tickForTest();
    const auto& surface = alignPane(c).surfaceForTest();
    REQUIRE(surface.gap().has_value());
    const auto bin = static_cast<std::size_t>(std::lround(verdict.crossoverHz / surface.binWidthHz()));
    REQUIRE(bin < surface.gap()->perBinDb.size());
    const double gapDb = surface.gap()->perBinDb[bin];
    WARN("T6 SUM gap at fc = " << gapDb << " dB (bound |gap| < 3), measured "
                               << surface.measuredSumDb()[bin] << " dB, predicted "
                               << surface.predictedSumDb()[bin] << " dB");
    CHECK(std::abs(gapDb) < 3.0);
}

TEST_CASE("align: LOCATE, CAL, SWEEP and APPLY are refused while ALIGN runs", kTag) {
    MainComponent c;
    c.setSyntheticMode(true);
    // APPLY needs an accepted LOCATE suggestion first; this is the real one.
    Access::locateClickedForTest(c);
    REQUIRE(tickUntil(c, [&] { return Access::delaySuggestionForTest(c).has_value(); }, 30000));
    REQUIRE(tickUntil(c, [&] { return !Access::locateActiveForTest(c); }, 5000));
    REQUIRE(Access::delaySuggestionForTest(c)->verdict == rta::dsp::DelayVerdict::Accepted);

    prepare(c);
    REQUIRE(tickUntil(c, [&] { return alignPane(c).measureButtonForTest().isEnabled(); }, 20000));
    alignPane(c).measureButtonForTest().onClick();
    REQUIRE(runnerOf(c).busy());
    CHECK(Access::captureBusyForTest(c));

    Access::locateClickedForTest(c);
    CHECK_FALSE(Access::locateActiveForTest(c));
    Access::calibrationStartClickedForTest(c);
    CHECK_FALSE(Access::calibrationArmedForTest(c));
    Access::sweepForTest(c).clicked();
    CHECK(Access::sweepForTest(c).stateForTest() == MainComponentSweep::State::Idle);
    CHECK(Access::sweepForTest(c).readoutForTest().contains("refused"));
    Access::applyClickedForTest(c);
    CHECK(Access::delayReadoutForTest(c).contains("ALIGN is running"));
    CHECK(runnerOf(c).busy());  // none of the four disturbed the run

    REQUIRE(tickUntil(c, [&] { return !runnerOf(c).busy(); }, 40000));
    CHECK(runnerOf(c).outcomeForTest() == AlignOutcome::Measured);
    CHECK_FALSE(Access::captureBusyForTest(c));
}

TEST_CASE("align: MEASURE is refused while a SWEEP holds the output", kTag) {
    MainComponent c;
    prepare(c);
    Access::sweepForTest(c).setDurationsForTest(2.0, 0.5);
    REQUIRE(tickUntil(c, [&] { return alignPane(c).measureButtonForTest().isEnabled(); }, 20000));
    Access::sweepForTest(c).clicked();
    REQUIRE(Access::sweepForTest(c).busy());

    Access::timerTickForTest(c);
    alignPane(c).tickForTest();
    CHECK_FALSE(alignPane(c).measureButtonForTest().isEnabled());
    CHECK(alignPane(c).blockerTextForTest().contains("LOCATE, CAL, SWEEP or VERIFY"));
    CHECK_FALSE(runnerOf(c).press());
    CHECK(runnerOf(c).lastRefusalForTest() == AlignBlock::CaptureBusy);
    CHECK_FALSE(runnerOf(c).busy());

    // Let the sweep finish so the component is not torn down mid-analysis.
    REQUIRE(tickUntil(c, [&] { return !Access::sweepForTest(c).busy(); }, 60000));
}

TEST_CASE("align: turning SYNTHETIC off mid-run aborts it and leaves the output disarmed", kTag) {
    MainComponent c;
    prepare(c);
    REQUIRE(tickUntil(c, [&] { return alignPane(c).measureButtonForTest().isEnabled(); }, 20000));
    alignPane(c).measureButtonForTest().onClick();
    REQUIRE(runnerOf(c).busy());

    c.setSyntheticMode(false);  // the loopback thread is gone; nothing renders any more
    Access::timerTickForTest(c);
    CHECK_FALSE(runnerOf(c).busy());
    CHECK(runnerOf(c).outcomeForTest() == AlignOutcome::OutputChanged);
    CHECK(runnerOf(c).status().find("input mode changed") != std::string::npos);
    CHECK_FALSE(Access::captureBusyForTest(c));

    // Disarmed means the engine falls quiet when it is next rendered: with the
    // source still armed it would never reach quiescence.
    auto& engine = Access::outputEngineForTest(c);
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    float* outs[2] = { left.data(), right.data() };
    for (int block = 0; block < 16; ++block) engine.render(outs, 2, 512);
    CHECK(engine.sourceIsQuiescent());
}

TEST_CASE("align: switching away and back keeps the answers, the verdict and the chart", kTag) {
    MainComponent c;
    prepare(c);
    measureToVerdict(c);
    REQUIRE(runnerOf(c).verdict().has_value());
    const juce::String before = alignPane(c).readoutForTest();
    REQUIRE(before.isNotEmpty());

    c.selectPaneView(rta::view::PaneSelectorButton::Rta);
    c.selectPaneView(rta::view::PaneSelectorButton::Align);  // a brand new pane
    auto& pane = alignPane(c);
    CHECK(pane.readoutForTest() == before);
    CHECK(pane.surfaceForTest().pointCount() > 0);
    CHECK(pane.highPassComboForTest().getSelectedId() == 1);
    CHECK(pane.topologyComboForTest().getSelectedId() == 2);
    CHECK(pane.inversionComboForTest().getSelectedId() == 1);
    CHECK(pane.seedComboForTest().getSelectedId() == 1);  // "NO SEED" is an answer, and it survived
}
