// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of tools/. See snapshot_align.h.
#include "snapshot_align.h"

#include "SnapshotRender.h"

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "dev-support/SyntheticCrossoverHp.h"
#include "measure/SyntheticCrossover.h"
#include "view/AlignPaneView.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

namespace rta::tools
{
namespace
{

using Access = MainComponentTestAccess;

// The 2 Hz timer does not run offscreen, so the pump drives the component's
// own handler and the pane's own timer by hand, bounded in wall-clock time.
template <typename Predicate>
bool pumpUntil (MainComponent& component, rta::view::AlignPaneView& pane, Predicate&& done, int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<std::uint32_t> (timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline)
    {
        Access::timerTickForTest (component);
        pane.tickForTest();
        if (done())
            return true;
        juce::Thread::sleep (5);
    }
    Access::timerTickForTest (component);
    pane.tickForTest();
    return done();
}

bool fail (const char* what, const char* fileName, int& failures)
{
    std::printf ("FAILED: %s -- %s\n", fileName, what);
    ++failures;
    return false;
}

// `useFixture` false: the SHIPPED SYNTHETIC defaults, what an operator gets
// (output 0 a 40 Hz - 16 kHz band-pass, output 1 an LR4 low-pass at 100 Hz).
// true: the plan's LR4 high-pass / low-pass pair, reachable only through the
// test hook, which is the pair the T6 bounds are derived from.
void renderOne (const juce::File& outDir, const char* fileName, bool useFixture, bool bindIrs, int& failures,
               int seedId = 1)
{
    constexpr double kFs = 48000.0;
    MainComponent component;
    component.setSyntheticMode (true);
    // H7 T13: measurements follow GEN's level (-30.0 by default); the specimen was settled at -12.
    MainComponentTestAccess::pinGeneratorLevelForTest (component, -12.0);
    // A sweep refuses to arm while the input is mid-resync (PR #78); the
    // fixture path below waits inside restartSyntheticForTest, this one must too.
    Access::waitForResyncForTest (component);

    if (useFixture)
    {
        rta::measure::SyntheticInput::Config config;
        config.sampleRate = kFs;
        config.measurementDelaySamples = 4;
        config.measurementFilter = rta::measure::lr4HighPass (100.0, kFs);
        config.secondOutputDelaySamples = 100;
        config.secondOutputFilter = rta::measure::lr4LowPass (100.0, kFs);
        Access::restartSyntheticForTest (component, config);
    }

    component.selectPaneView (rta::view::PaneSelectorButton::Align);
    auto* pane = dynamic_cast<rta::view::AlignPaneView*> (
        const_cast<juce::Component*> (&Access::pane (component)));
    if (pane == nullptr)
    {
        fail ("the ALIGN pane did not build", fileName, failures);
        return;
    }
    auto& runner = Access::alignForTest (component).runnerForTest();

    pane->highPassComboForTest().setSelectedId (1, juce::sendNotificationSync);   // MAIN is high-pass
    pane->topologyComboForTest().setSelectedId (2, juce::sendNotificationSync);   // LR-4
    pane->inversionComboForTest().setSelectedId (1, juce::sendNotificationSync);  // NOT INVERTED
    pane->seedComboForTest().setSelectedId (seedId, juce::sendNotificationSync);  // 1 = NO SEED; 4 = SEED 100 HZ

    if (bindIrs)
    {
        // Wave B: SWEEP MAIN then SWEEP SUB through the pane's own buttons. Each
        // sweep binds its IR to the side that asked, so MEASURE needs no typed offset.
        auto& sweep = Access::sweepForTest (component);
        const double sweepSeconds = useFixture ? 2.0 : 10.0;
        const double tailSeconds = useFixture ? 0.5 : 3.0;
        sweep.setDurationsForTest (sweepSeconds, tailSeconds);  // the shipped band-pass needs the long sweep
        // The loopback is paced by the wall clock and the analysis shares the cores, so
        // the waits below are a function of the sweep asked for (same derivation as
        // app/tests_juce/AlignIrFixture.h): kSlack x (pre-roll + sweep + tail), floored at
        // the former 60 s. The sweep's own capture timeout gets the same stretch.
        constexpr double kBudgetSlack = 6.0;
        sweep.setTimeoutSlackForTest (5000.0 * kBudgetSlack);
        const int sweepBudgetMs = std::max (60000, static_cast<int> (1000.0 * kBudgetSlack * (sweepSeconds + tailSeconds + 1.0)));
        for (auto* button : { &pane->irPanelForTest().sweepMainButtonForTest(),
                              &pane->irPanelForTest().sweepSubButtonForTest() })
        {
            if (! pumpUntil (component, *pane, [&] { return button->isEnabled(); }, 20000))
            {
                fail ("a SWEEP button never became enabled", fileName, failures);
                return;
            }
            button->onClick();
            if (! pumpUntil (component, *pane, [&] { return ! sweep.busy(); }, sweepBudgetMs))
            {
                fail ("a sweep did not finish", fileName, failures);
                return;
            }
        }
        if (runner.boundIr (rta::measure::Source::Main) == nullptr || runner.boundIr (rta::measure::Source::Sub) == nullptr)
        {
            // Name the sweep's own verdict: a refusal or abort says why, a bare "did not bind" does not.
            const std::string why = sweep.readoutForTest().toStdString();
            std::printf ("sweep readout: %s\n", why.c_str());
            fail ("a sweep did not bind its IR", fileName, failures);
            return;
        }
    }

    if (! pumpUntil (component, *pane, [&] { return pane->measureButtonForTest().isEnabled(); }, 20000))
    {
        fail ("MEASURE never became enabled (no TRANSFER snapshot)", fileName, failures);
        return;
    }
    pane->measureButtonForTest().onClick();
    if (! pumpUntil (component, *pane, [&] { return ! runner.busy(); }, 40000) || ! runner.verdict().has_value())
    {
        fail ("MEASURE did not reach a verdict", fileName, failures);
        return;
    }
    if (bindIrs && seedId != 1)
    {
        // The seed disagrees with the crossing: the run must ASK, with the
        // note and both hints. Render that screen as it stands.
        if (runner.step() != rta::measure::WizardStep::AskingCycle || ! runner.seedDisagreement().has_value())
        {
            fail ("the seed did not disagree with the crossing", fileName, failures);
            return;
        }
        pane->tickForTest();
        if (! renderComponent (component, outDir, fileName, 1280, 800))
            ++failures;
        return;
    }
    if (bindIrs && runner.step() == rta::measure::WizardStep::AskingCycle)
    {
        fail ("the bound IRs did not give the cycle integer", fileName, failures);
        return;
    }
    if (! bindIrs)
    {
        pane->offsetEditorForTest().setText ("2.0", juce::dontSendNotification);
        pane->offsetButtonForTest().onClick();
    }
    // The last step's excitation ramps out before SUM may take the output.
    pumpUntil (component, *pane, [&] { return Access::outputSourceQuiescentForTest (component); }, 20000);
    if (! pane->sumButtonForTest().isEnabled())
    {
        fail ("SUM stayed disabled after the coarse offset", fileName, failures);
        return;
    }
    pane->sumButtonForTest().onClick();
    const bool sumDone = pumpUntil (component, *pane, [&] { return ! runner.busy(); }, 40000);
    pane->tickForTest();  // the pane redraws on the revision the finished SUM bumped
    if (! sumDone || ! pane->surfaceForTest().gap().has_value())
    {
        fail ("SUM did not produce a measured sum", fileName, failures);
        return;
    }
    if (! renderComponent (component, outDir, fileName, 1280, 800))
        ++failures;
}

} // namespace

void renderAlignSpecimen (const juce::File& outDir, int& failures)
{
    renderOne (outDir, "main-live-align.png", false, false, failures);
    renderOne (outDir, "main-live-align-lr4.png", true, false, failures);
    renderOne (outDir, "main-live-align-ir.png", true, true, failures);
    // H5 closeout D9/D3: shipped defaults, IRs bound, SEED 100 HZ against a ~58 Hz
    // crossing -- the longest readout (the disagreement note and both hints).
    renderOne (outDir, "main-live-align-seed.png", false, true, failures, 4);
}

} // namespace rta::tools
