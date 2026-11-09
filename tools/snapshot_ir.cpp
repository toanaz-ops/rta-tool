// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of tools/. See snapshot_ir.h.
#include "snapshot_ir.h"

#include "SnapshotRender.h"

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "view/IrPaneView.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace rta::tools
{
namespace
{

using Access = MainComponentTestAccess;

constexpr double kFs = 48000.0;
constexpr std::size_t kOrigin = 2000;
constexpr long kArrival = 360;  // 7.5 ms of flight

// A direct sound, reflections at +2.5, +6 and +12.5 ms, and a diffuse tail that
// falls 60 dB in 0.4 s. Deterministic (a fixed LCG), so the picture is the same
// every run.
std::shared_ptr<const rta::measure::SweepIr> syntheticIr()
{
    auto ir = std::make_shared<rta::measure::SweepIr>();
    ir->samples.assign(kOrigin + 48000, 0.0f);
    ir->originIndex = kOrigin;
    ir->arrivalSamples = kArrival;
    ir->sampleRate = kFs;
    const std::size_t arrival = kOrigin + static_cast<std::size_t> (kArrival);
    std::uint32_t state = 12345u;
    for (std::size_t i = arrival + 24; i < ir->samples.size(); ++i)
    {
        state = state * 1664525u + 1013904223u;
        const double noise = (static_cast<double> (state >> 8) / 8388608.0) - 1.0;
        const double t = static_cast<double> (i - arrival) / kFs;
        ir->samples[i] = static_cast<float> (0.06 * noise * std::pow (10.0, -3.0 * t / 0.4));
    }
    ir->samples[arrival] = 1.0f;
    ir->samples[arrival + 120] = -0.45f;
    ir->samples[arrival + 288] = 0.30f;
    ir->samples[arrival + 600] = -0.40f;
    return ir;
}

} // namespace

void renderIrSpecimen (const juce::File& outDir, int& failures)
{
    constexpr const char* fileName = "ir-view.png";
    MainComponent component;
    component.setSyntheticMode (true);

    rta::trace::CaptureMeta meta;
    meta.id = "specimen-ir";
    meta.sampleRate = kFs;
    meta.fftSize = 64;
    auto stub = rta::trace::Trace::make (meta, std::vector<float> (rta::trace::pointCountFor (64), -6.0f));
    const auto id = Access::libraryForTest (component).add (std::move (*stub), "SWEEP out1 @ 10:00:00", "SWEEP");
    Access::sweepForTest (component).addIrForTest (id, syntheticIr());

    component.selectPaneView (rta::view::PaneSelectorButton::Ir);
    auto* pane = dynamic_cast<rta::view::IrPaneView*> (const_cast<juce::Component*> (&Access::pane (component)));
    if (pane == nullptr)
    {
        std::printf ("FAILED: the IR pane did not build -- %s\n", fileName);
        ++failures;
        return;
    }
    pane->tickForTest();
    pane->irComboForTest().setSelectedId (1, juce::sendNotificationSync);
    const auto analysis = Access::irForTest (component).analysisForTest();
    if (analysis == nullptr || ! analysis->ok)
    {
        std::printf ("FAILED: the default gate was refused -- %s\n", fileName);
        ++failures;
        return;
    }
    if (! renderComponent (component, outDir, fileName, 1280, 800))
        ++failures;
}

} // namespace rta::tools
