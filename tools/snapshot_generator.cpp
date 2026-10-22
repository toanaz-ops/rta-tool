// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of tools/. See snapshot_generator.h.
#include "snapshot_generator.h"

#include "SnapshotRender.h"

#include "MainComponent.h"
#include "MainComponentTestAccess.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace rta::tools
{
namespace
{

using Access = MainComponentTestAccess;

// The 2 Hz timer does not run offscreen, so the pump drives the component's own
// handler by hand, bounded in wall-clock time.
template <typename Predicate>
bool pumpUntil (MainComponent& component, Predicate&& done, int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<std::uint32_t> (timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline)
    {
        Access::timerTickForTest (component);
        if (done())
            return true;
        juce::Thread::sleep (5);
    }
    Access::timerTickForTest (component);
    return done();
}

/// True when the RTA's strongest bin is within one bin of `hz` and stands 25 dB
/// above the spectrum's median (a tone, not the SYNTHETIC noise floor).
bool toneAt (MainComponent& component, double hz)
{
    const auto snapshot = Access::analysisThread (component).latest();
    if (! snapshot || snapshot->spectrumDb.size() < 8 || snapshot->fftSize == 0)
        return false;
    const auto& s = snapshot->spectrumDb;
    const auto top = std::max_element (s.begin() + 1, s.end());
    std::vector<float> sorted (s.begin() + 1, s.end());
    std::nth_element (sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t> (sorted.size() / 2), sorted.end());
    const double binHz = snapshot->sampleRate / static_cast<double> (snapshot->fftSize);
    const double peakHz = static_cast<double> (top - s.begin()) * binHz;
    return std::abs (peakHz - hz) <= binHz && *top - sorted[sorted.size() / 2] > 25.0f;
}

} // namespace

void renderGeneratorSpecimen (const juce::File& outDir, int& failures)
{
    constexpr const char* fileName = "main-live-gen.png";
    MainComponent component;
    component.setSyntheticMode (true);
    Access::waitForResyncForTest (component);

    auto& gen = Access::generatorForTest (component);
    gen.signalSelected (rta::measure::GeneratorSignal::Sine);
    gen.frequencySelected (1000);
    gen.genClicked();
    // The level and the output are the launch defaults: -30.0 dBFS, out 1.

    const juce::String expected = "GEN: playing sine 1000 Hz -30.0 dBFS peak on out 1";
    if (! pumpUntil (component, [&] { return gen.readoutForTest() == expected; }, 5000))
    {
        std::printf ("FAILED: %s -- the generator is not playing (readout \"%s\")\n", fileName,
                     gen.readoutForTest().toRawUTF8());
        ++failures;
        return;
    }
    if (! pumpUntil (component, [&] { return toneAt (component, 1000.0); }, 20000))
    {
        std::printf ("FAILED: %s -- no 1 kHz tone appeared in the RTA spectrum\n", fileName);
        ++failures;
        return;
    }
    if (! renderComponent (component, outDir, fileName, 1280, 800))
        ++failures;
}

void renderOwnSpecimen (const juce::File& outDir, int& failures)
{
    constexpr const char* fileName = "main-live-own.png";
    MainComponent component;
    component.setSyntheticMode (true);
    Access::waitForResyncForTest (component);

    auto& gen = Access::generatorForTest (component);
    gen.measurementModeClicked();
    for (int i = 0; i < 8; ++i)
        gen.ownStepClicked (-1);  // -12 -> -20
    auto& sweep = Access::sweepForTest (component);
    sweep.clicked();
    const juce::String expected = "sweep: arming @ -20.0 dBFS peak";
    if (! sweep.busy() || ! sweep.readoutForTest().startsWith (expected)
        || gen.measButtonTextForTest() != "OWN -20.0")
    {
        std::printf ("FAILED: %s -- OWN mode did not arm the sweep at -20.0 (readout \"%s\", MEAS \"%s\")\n", fileName,
                     sweep.readoutForTest().toRawUTF8(), gen.measButtonTextForTest().toRawUTF8());
        ++failures;
        return;
    }
    if (! renderComponent (component, outDir, fileName, 1280, 800))
        ++failures;
}

} // namespace rta::tools
