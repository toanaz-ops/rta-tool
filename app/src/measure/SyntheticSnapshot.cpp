// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// See docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.3, §3.4.
#include "measure/SyntheticSnapshot.h"

#include "measure/Levels.h"

#include "rta/gen/Synthetic.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace rta::measure {

namespace {

constexpr std::size_t kChunkSamples = 1024;

/// A pink block size must be a power of two, and this fixture always wants
/// the same one for a given fftSize so the loop period lines up predictably
/// with the analysis frame size. Any power-of-two >= 4 would satisfy
/// SyntheticPink's contract; matching fftSize is simplest and needs no
/// extra field on SyntheticSpec.
std::size_t pinkBlockSize(const Analyser::Config& config) {
    return std::max<std::size_t>(config.fftSize, 4);
}

}  // namespace

SnapshotPtr makeSyntheticSnapshot(const SyntheticSpec& spec) {
    Analyser analyser(spec.analysis);

    // Peak amplitude -> the project's sine-referenced dBFS, via the ONE
    // conversion Levels.h defines: mean square of a peak-A signal read the
    // same way a sine's is, amplitude^2/2.
    const double targetDbFs = levelDbFs(spec.amplitude * spec.amplitude * 0.5);

    const auto totalSamples =
        static_cast<std::size_t>(std::llround(spec.seconds * spec.analysis.sampleRate));

    std::vector<float> chunk(kChunkSamples);

    if (spec.source == SyntheticSpec::Source::Sine) {
        rta::gen::SyntheticSine sine(spec.analysis.sampleRate, spec.sineHz, targetDbFs);
        std::size_t remaining = totalSamples;
        while (remaining > 0) {
            const std::size_t n = std::min(kChunkSamples, remaining);
            std::span<float> block(chunk.data(), n);
            sine.render(block);
            analyser.pushMeasurement(block);
            remaining -= n;
        }
    } else {
        rta::gen::SyntheticPink pink(pinkBlockSize(spec.analysis), targetDbFs, spec.seed);
        std::size_t remaining = totalSamples;
        while (remaining > 0) {
            const std::size_t n = std::min(kChunkSamples, remaining);
            std::span<float> block(chunk.data(), n);
            pink.render(block);
            analyser.pushMeasurement(block);
            remaining -= n;
        }
    }

    // No threads, no timing: publish once, offline, at the end -- this is
    // what makes the result bit-identical for the same spec.
    return analyser.publish(0);
}

}  // namespace rta::measure
