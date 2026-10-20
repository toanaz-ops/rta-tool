// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of tools/. See snapshot_align.h.
#include "snapshot_align.h"

#include "SnapshotRender.h"

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "dev-support/SyntheticCrossoverHp.h"
#include "measure/SyntheticCrossover.h"
#include "view/AlignPaneView.h"

#include <cstdint>
#include <cstdio>
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
void renderOne (const juce::File& outDir, const char* fileName, bool useFixture, bool bindIrs, int& failures)
{
    constexpr double kFs = 48000.0;
    MainComponent component;
    component.setSyntheticMode (true);

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
    pane->seedComboForTest().setSelectedId (1, juce::sendNotificationSync);       // NO SEED

    if (bindIrs)
    {
        // Wave B: SWEEP MAIN then SWEEP SUB through the pane's own buttons. Each
        // sweep binds its IR to the side that asked, so MEASURE needs no typed offset.
        auto& sweep = Access::sweepForTest (component);
        sweep.setDurationsForTest (2.0, 0.5);
        for (auto* button : { &pane->irPanelForTest().sweepMainButtonForTest(),
                              &pane->irPanelForTest().sweepSubButtonForTest() })
        {
            if (! pumpUntil (component, *pane, [&] { return button->isEnabled(); }, 20000))
            {
                fail ("a SWEEP button never became enabled", fileName, failures);
                return;
            }
            button->onClick();
            if (! pumpUntil (component, *pane, [&] { return ! sweep.busy(); }, 60000))
            {
                fail ("a sweep did not finish", fileName, failures);
                return;
            }
        }
        if (runner.boundIr (rta::measure::Source::Main) == nullptr || runner.boundIr (rta::measure::Source::Sub) == nullptr)
        {
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
}

} // namespace rta::tools
