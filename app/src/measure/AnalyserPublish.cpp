// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Split out of Analyser.cpp's publish() (task B0); see that header's own
// comment. No behaviour change from the code these bodies had at
// Analyser.cpp:201-269 before the split.
#include "measure/AnalyserPublish.h"

#include <numbers>

namespace rta::measure {

TransferBlock makeTransferBlock(const rta::dsp::TransferSnapshot& tf, int appliedDelaySamples) {
    TransferBlock block;
    block.magnitudeDb = tf.magnitudeDb;
    block.phaseDeg.resize(tf.phaseRadians.size());
    // The one radians -> degrees crossing in the whole application: core
    // wraps to (-pi, pi] because that is the natural output of a complex
    // division, but every consumer in view/ works in degrees because
    // PlotGeometry's phase pane runs +180 to -180.
    for (std::size_t i = 0; i < tf.phaseRadians.size(); ++i) {
        block.phaseDeg[i] = tf.phaseRadians[i] * static_cast<float>(180.0 / std::numbers::pi);
    }
    // Copies the optional itself, not its value -- absence of coherence
    // (below the effective-average gate) must survive this hop unchanged.
    block.coherence = tf.coherence;
    block.effectiveAverages = tf.effectiveAverages;
    block.appliedDelaySamples = appliedDelaySamples;
    return block;
}

MtwBlock makeMtwBlock(const rta::dsp::MtwResult& mtwResult, int appliedDelaySamples) {
    MtwBlock block;
    block.frequencyHz = mtwResult.frequencyHz;
    block.magnitudeDb = mtwResult.magnitudeDb;
    block.phaseDeg.resize(mtwResult.phaseRadians.size());
    for (std::size_t i = 0; i < mtwResult.phaseRadians.size(); ++i) {
        block.phaseDeg[i] = mtwResult.phaseRadians[i] * static_cast<float>(180.0 / std::numbers::pi);
    }
    // 0.0f, not left uninitialised, for indices whose owning band has not yet
    // passed its own gate -- MtwBandDescriptor::coherenceAvailable is what a
    // reader must check before trusting an entry here, never the value
    // itself.
    block.coherence.assign(mtwResult.frequencyHz.size(), 0.0f);
    block.bands.reserve(mtwResult.bands.size());
    for (std::size_t b = 0; b < mtwResult.bands.size(); ++b) {
        const auto& band = mtwResult.bands[b];
        const auto& bandSnapshot = mtwResult.bandSnapshots[b];

        MtwBandDescriptor descriptor;
        descriptor.firstIndex = band.firstIndex;
        descriptor.pointCount = band.lastBin - band.firstBin + 1;
        descriptor.fftSize = band.fftSize;
        descriptor.windowSeconds = static_cast<float>(band.windowSeconds);
        descriptor.integrationSeconds = static_cast<float>(band.integrationSeconds);
        descriptor.effectiveAverages = bandSnapshot.effectiveAverages;
        descriptor.seamHz = static_cast<float>(band.lowerEdgeHz);
        descriptor.coherenceAvailable = bandSnapshot.coherence.has_value();

        if (descriptor.coherenceAvailable) {
            for (std::size_t i = 0; i < descriptor.pointCount; ++i) {
                block.coherence[descriptor.firstIndex + i] =
                    (*bandSnapshot.coherence)[band.firstBin + i];
            }
        }
        block.bands.push_back(descriptor);
    }
    block.appliedDelaySamples = appliedDelaySamples;
    return block;
}

}  // namespace rta::measure
