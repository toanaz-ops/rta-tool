// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of tools/. See snapshot_rt60.h.
#include "snapshot_rt60.h"

#include "SnapshotRender.h"

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "dev-support/SyntheticRoomComb.h"
#include "view/Rt60PaneView.h"

#include <cstdint>
#include <cstdio>

namespace rta::tools
{
namespace
{

using Access = MainComponentTestAccess;

// The 2 Hz timer does not run offscreen, so the pump drives the component's own
// handler and the pane's own timer by hand, bounded in wall-clock time.
template <typename Predicate>
bool pumpUntil (MainComponent& component, rta::view::Rt60PaneView& pane, Predicate&& done, int timeoutMs)
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

} // namespace

void renderRt60Specimen (const juce::File& outDir, int& failures)
{
    constexpr const char* fileName = "main-live-rt60.png";
    constexpr double kFs = 48000.0;
    MainComponent component;
    component.setSyntheticMode (true);

    rta::measure::SyntheticInput::Config config;
    config.sampleRate = kFs;
    config.measurementDelaySamples = 4;
    config.measurementNoiseDb = -30.0;
    config.roomCombDelaySamples = 480;
    config.roomCombGain = static_cast<float> (rta::measure::roomCombGainFor (0.80, 480, kFs));
    Access::restartSyntheticForTest (component, config);
    // A 1 s sweep keeps the specimen to seconds; the TAIL stays the pane's default 3 s.
    auto& sweep = Access::sweepForTest (component);
    sweep.setDurationsForTest (1.0, 3.0);

    component.selectPaneView (rta::view::PaneSelectorButton::Rt60);
    auto* pane = dynamic_cast<rta::view::Rt60PaneView*> (const_cast<juce::Component*> (&Access::pane (component)));
    if (pane == nullptr)
    {
        fail ("the RT60 pane did not build", fileName, failures);
        return;
    }

    if (! pumpUntil (component, *pane, [&] { return pane->sweepButtonForTest().isEnabled(); }, 20000))
    {
        fail ("the pane's SWEEP button never became enabled", fileName, failures);
        return;
    }
    pane->sweepButtonForTest().onClick();
    if (! pumpUntil (component, *pane, [&] { return ! sweep.busy(); }, 90000))
    {
        fail ("the sweep did not finish", fileName, failures);
        return;
    }
    pane->tickForTest();  // the pane redraws on the revision the finished sweep bumped

    const auto result = Access::rt60ForTest (component).resultForTest();
    bool anyT30 = false;
    if (result != nullptr)
        for (std::size_t i = 1; i < result->bands.size(); ++i)  // 125 Hz and up
            anyT30 = anyT30 || result->bands[i].t30.has();
    if (result == nullptr || ! result->analysed() || ! anyT30)
    {
        fail ("no octave from 125 Hz up reported a T30", fileName, failures);
        return;
    }
    if (! renderComponent (component, outDir, fileName, 1280, 800))
        ++failures;

    // The same IR in third-octave bands: the toggle, the worker thread, the table
    // of 24 rows and the chart under it. A click on the 1 kHz third feeds the chart.
    constexpr const char* thirdName = "main-live-rt60-third.png";
    pane->bandSetButtonForTest().onClick();
    if (! pumpUntil (component, *pane, [&] { return ! Access::rt60ForTest (component).analysingForTest(); }, 60000))
    {
        fail ("the third-octave analysis did not finish", thirdName, failures);
        return;
    }
    pane->clickRowForTest (13);
    if (pane->tableLinesForTest().size() != 25)
    {
        fail ("the third-octave table is not 24 rows", thirdName, failures);
        return;
    }
    if (! renderComponent (component, outDir, thirdName, 1280, 800))
        ++failures;
}

} // namespace rta::tools
