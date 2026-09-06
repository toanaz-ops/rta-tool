// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/MtwEngine.h"

#include <stdexcept>
#include <utility>

namespace rta::dsp {

namespace {

/// One band's own DualFftEngine::Config. Every field a caller could confuse
/// across bands is set HERE, explicitly, rather than copied from a shared
/// object and mutated in place -- refutation R13 in the plan is exactly the
/// bug that shortcut would reintroduce (all bands reporting the same
/// timeConstantSeconds because they shared the object it came from).
DualFftEngine::Config bandConfig(const MtwConfig& config, const MtwBand& band) {
    DualFftEngine::Config c;
    c.fftSize = band.fftSize;
    c.hopSize = band.hopSize;
    c.sampleRate = config.sampleRate;
    c.window = config.window;
    c.averaging = config.averaging;

    // Memory rule 1 (record §2's memory section): DualFftEngine allocates its
    // FIFO unconditionally, whatever the averaging mode, so Exponential mode
    // must be told fifoDepth = 1 or it carries 31 MB nothing reads.
    c.fifoDepth = (config.averaging == TransferAveraging::Fifo) ? config.fifoDepth : 1;

    // The ONE place seconds are derived in this lane (MtwLayout.h's mtwAlpha
    // comment): timeConstantFrames * hop_k/fs, so DualFftEngine's own
    // alpha = 1 - exp(-(hop_k/fs)/timeConstantSeconds) collapses the hop back
    // out to 1 - exp(-1/timeConstantFrames) -- identical in every band. Set
    // whatever the mode, so a later switch to Exponential is never a silent
    // zero.
    c.timeConstantSeconds =
        config.timeConstantFrames * static_cast<double>(band.hopSize) / config.sampleRate;

    // Every band runs at full rate (record §2's "no decimation" decision), so
    // the SAME integer sample count compensates the delay in every band --
    // there is no fractional path to invent and nothing to convert.
    c.referenceDelaySamples = config.referenceDelaySamples;
    c.minimumEffectiveAverages = config.minimumEffectiveAverages;
    return c;
}

}  // namespace

MtwEngine::MtwEngine(const MtwConfig& config)
    : config_(config), bands_(mtwBands(config)) {
    engines_.reserve(bands_.size());
    for (const auto& band : bands_) {
        engines_.push_back(std::make_unique<DualFftEngine>(bandConfig(config_, band)));
    }
}

void MtwEngine::process(std::span<const float> reference, std::span<const float> measurement) {
    // Nothing but forwarding: allocation happened once, in the constructor.
    // A dropout at a live show is the exact situation this tool exists for.
    for (auto& engine : engines_) {
        engine->process(reference, measurement);
    }
}

void MtwEngine::reset() noexcept {
    for (auto& engine : engines_) {
        engine->reset();
    }
}

std::size_t bandForIndex(const MtwResult& result, std::size_t index) {
    for (std::size_t b = 0; b < result.bands.size(); ++b) {
        const auto& band = result.bands[b];
        const std::size_t count = band.lastBin - band.firstBin + 1;
        if (index >= band.firstIndex && index < band.firstIndex + count) {
            return b;
        }
    }
    throw std::out_of_range("bandForIndex: stitched index out of range");
}

std::optional<float> coherenceAt(const MtwResult& result, std::size_t index) {
    const std::size_t b = bandForIndex(result, index);
    const auto& band = result.bands[b];
    const std::size_t localBin = band.firstBin + (index - band.firstIndex);
    // No alias bound to the field itself (check_coherence_gate.cmake's own
    // header warns that a reference bound directly to `.coherence` can be
    // assigned through later without the guard ever seeing an `=` next to
    // the field name) -- index through `result.bandSnapshots[b]` each time.
    if (!result.bandSnapshots[b].coherence.has_value()) {
        return std::nullopt;
    }
    return (*result.bandSnapshots[b].coherence)[localBin];
}

MtwResult makeMtwResult(const MtwEngine& engine, Estimator estimator) {
    MtwResult result;
    result.config = engine.config();
    result.bands = mtwBands(result.config);
    result.frequencyHz = mtwFrequencies(result.config);

    const std::size_t total = result.frequencyHz.size();
    result.h.resize(total);
    result.magnitudeDb.resize(total);
    result.phaseRadians.resize(total);
    result.bandSnapshots.reserve(engine.bandCount());

    for (std::size_t b = 0; b < engine.bandCount(); ++b) {
        // The ONE call that gates coherence (conflict C4): this file never
        // assigns TransferSnapshot::coherence itself, so
        // check_coherence_gate.cmake's sentinel exception stays the only
        // place that happens.
        auto snapshot = makeSnapshot(engine.band(b), estimator);
        const auto& band = result.bands[b];

        for (std::size_t bin = band.firstBin; bin <= band.lastBin; ++bin) {
            const std::size_t index = band.firstIndex + (bin - band.firstBin);
            result.h[index] = snapshot.h[bin];
            result.magnitudeDb[index] = snapshot.magnitudeDb[bin];
            result.phaseRadians[index] = snapshot.phaseRadians[bin];
        }

        result.bandSnapshots.push_back(std::move(snapshot));
    }

    return result;
}

}  // namespace rta::dsp
