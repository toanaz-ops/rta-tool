// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of tools/. See snapshot_wav.h.
#include "snapshot_wav.h"

#include "SnapshotRender.h"

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "WavFileWriter.h"
#include "WavTransferFixture.h"
#include "trace/TraceGroups.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace rta::tools
{

void renderWavSpecimen (const juce::File& outDir, int& failures)
{
    constexpr const char* fileName = "main-wav.png";
    using Access = MainComponentTestAccess;

    std::vector<std::vector<float>> channels (4);
    wavfix::makePair (480000, 5, [] (std::size_t) { return 37; }, channels[0], channels[2]);
    channels[1] = wavfix::pink (480000, 77);
    channels[3] = wavfix::pink (480000, 78);
    float peak = 0.0f;
    for (const auto& c : channels)
        for (float v : c)
            peak = std::max (peak, std::abs (v));
    for (auto& c : channels)
        for (float& v : c)
            v *= 0.9f / peak;

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rta_snapshot_pink.wav");
    if (! wavfile::write (file, wavfile::Container::Wav, wavfile::Sample::Pcm24, wavfix::kFs, channels))
    {
        std::printf ("FAILED: could not write the specimen WAV -- %s\n", fileName);
        ++failures;
        return;
    }

    MainComponent component;
    component.setSyntheticMode (true);
    component.selectPaneView (rta::view::PaneSelectorButton::Transfer);
    auto& wav = Access::wavForTest (component);
    bool ok = Access::performLoadWavForTest (component, file);
    if (ok)
    {
        wav.setControlsForTest (1, 3, 0.0, 0.0);
        wav.analyseClicked();
        ok = wav.waitForJobForTest (60000);
    }
    std::size_t stored = 0;
    for (const auto& e : Access::libraryForTest (component).entries())
        stored += e.group == rta::trace::kWavGroup ? 1 : 0;
    if (! ok || stored != 1)
    {
        std::printf ("FAILED: the WAV analysis stored %zu traces -- %s\n", stored, fileName);
        ++failures;
        file.deleteFile();
        return;
    }
    if (! renderComponent (component, outDir, fileName, 1280, 1100))
        ++failures;
    file.deleteFile();
}

} // namespace rta::tools
