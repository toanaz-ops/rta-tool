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

bool fail (const char* what, int& failures)
{
    std::printf ("FAILED: main-live-align.png -- %s\n", what);
    ++failures;
    return false;
}

} // namespace

void renderAlignSpecimen (const juce::File& outDir, int& failures)
{
    constexpr double kFs = 48000.0;
    MainComponent component;
    component.setSyntheticMode (true);

    rta::measure::SyntheticInput::Config config;
    config.sampleRate = kFs;
    config.measurementDelaySamples = 4;
    config.measurementFilter = rta::measure::lr4HighPass (100.0, kFs);
    config.secondOutputDelaySamples = 100;
    config.secondOutputFilter = rta::measure::lr4LowPass (100.0, kFs);
    Access::restartSyntheticForTest (component, config);

    component.selectPaneView (rta::view::PaneSelectorButton::Align);
    auto* pane = dynamic_cast<rta::view::AlignPaneView*> (
        const_cast<juce::Component*> (&Access::pane (component)));
    if (pane == nullptr)
    {
        fail ("the ALIGN pane did not build", failures);
        return;
    }
    auto& runner = Access::alignForTest (component).runnerForTest();

    pane->highPassComboForTest().setSelectedId (1, juce::sendNotificationSync);   // MAIN is high-pass
    pane->topologyComboForTest().setSelectedId (2, juce::sendNotificationSync);   // LR-4
    pane->inversionComboForTest().setSelectedId (1, juce::sendNotificationSync);  // NOT INVERTED
    pane->seedComboForTest().setSelectedId (1, juce::sendNotificationSync);       // NO SEED

    if (! pumpUntil (component, *pane, [&] { return pane->measureButtonForTest().isEnabled(); }, 20000))
    {
        fail ("MEASURE never became enabled (no TRANSFER snapshot)", failures);
        return;
    }
    pane->measureButtonForTest().onClick();
    if (! pumpUntil (component, *pane, [&] { return ! runner.busy(); }, 40000) || ! runner.verdict().has_value())
    {
        fail ("MEASURE did not reach a verdict", failures);
        return;
    }
    pane->offsetEditorForTest().setText ("2.0", juce::dontSendNotification);
    pane->offsetButtonForTest().onClick();
    if (! pane->sumButtonForTest().isEnabled())
    {
        fail ("SUM stayed disabled after the coarse offset", failures);
        return;
    }
    pane->sumButtonForTest().onClick();
    const bool sumDone = pumpUntil (component, *pane, [&] { return ! runner.busy(); }, 40000);
    pane->tickForTest();  // the pane redraws on the revision the finished SUM bumped
    if (! sumDone || ! pane->surfaceForTest().gap().has_value())
    {
        fail ("SUM did not produce a measured sum", failures);
        return;
    }
    if (! renderComponent (component, outDir, "main-live-align.png", 1280, 800))
        ++failures;
}

} // namespace rta::tools
