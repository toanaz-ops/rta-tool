// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/MtwLayout.h"

#include <cmath>
#include <stdexcept>

namespace rta::dsp {

namespace {

/// Past this, N0*2^K stops being a table this machine should allocate: at
/// N0=2048, K=10 the bottom band is a 2Mi-point transform, ~32 MB for one
/// ring alone (record §2's memory section). K = 10 is kept legal because
/// task 1 case 6 needs a K it can still build; nothing past it is.
constexpr std::size_t kMaxOctaveCount = 10;
constexpr std::size_t kMaxFifoDepth = 32;  ///< record §2: 66.6 MB of FIFO at 32.

bool isLegalTopFftSize(std::size_t n) {
    return n == 512 || n == 1024 || n == 2048;
}

/// fftSize of the band at ascending-frequency index `i` (0 = bottom, largest
/// FFT). The header's ORDERING WARNING applies: the record's own `k` is
/// `octaveCount - i`, so this is `topFftSize * 2^(octaveCount - i)`.
std::size_t fftSizeForIndex(const MtwConfig& config, std::size_t i) {
    const std::size_t k = config.octaveCount - i;
    return config.topFftSize << k;
}

}  // namespace

void validate(const MtwConfig& config) {
    if (!isLegalTopFftSize(config.topFftSize)) {
        throw std::invalid_argument("MtwConfig topFftSize must be 512, 1024 or 2048");
    }
    if (config.octaveCount == 0 || config.octaveCount > kMaxOctaveCount) {
        throw std::invalid_argument("MtwConfig octaveCount must be within 1..10");
    }
    if (!(config.sampleRate > 0.0)) {
        throw std::invalid_argument("MtwConfig sampleRate must be positive");
    }
    if (!(config.timeConstantFrames > 0.0)) {
        throw std::invalid_argument("MtwConfig timeConstantFrames must be positive");
    }
    // Same floor DualFftEngine::validate enforces (core/src/dsp/DualFftEngine.cpp):
    // below 1.0 effective average, coherence is definitionally 1.0 everywhere.
    if (!(config.minimumEffectiveAverages >= 1.0)) {
        throw std::invalid_argument("MtwConfig minimumEffectiveAverages must be at least 1.0");
    }
    if (config.fifoDepth == 0 || config.fifoDepth > kMaxFifoDepth) {
        throw std::invalid_argument("MtwConfig fifoDepth must be within 1..32");
    }
}

std::vector<MtwBand> mtwBands(const MtwConfig& config) {
    validate(config);

    const std::size_t bandCount = config.octaveCount + 1;
    std::vector<MtwBand> bands(bandCount);
    std::size_t runningIndex = 0;

    for (std::size_t i = 0; i < bandCount; ++i) {
        MtwBand band;
        band.fftSize = fftSizeForIndex(config, i);
        band.hopSize = band.fftSize / 4;

        // Conflict C1: the owned octave is [fs/(8*2^k), fs/(4*2^k)), so the
        // lower bin index is f_lo * N_k / fs = N_k / (8*2^k) = N0/8,
        // independent of k -- true for every band but the bottom, which owns
        // down to DC instead because there is no band below it to hand DC to.
        const bool isBottom = (i == 0);
        const bool isTop = (i == bandCount - 1);
        band.firstBin = isBottom ? 0 : config.topFftSize / 8;
        band.lastBin = isTop ? config.topFftSize / 2 : config.topFftSize / 4 - 1;

        band.firstIndex = runningIndex;
        runningIndex += band.lastBin - band.firstBin + 1;

        band.lowerEdgeHz = isBottom ? 0.0
                                    : static_cast<double>(band.firstBin) * config.sampleRate /
                                          static_cast<double>(band.fftSize);
        band.windowSeconds = static_cast<double>(band.fftSize) / config.sampleRate;
        band.integrationSeconds = static_cast<double>(config.fifoDepth) *
                                   static_cast<double>(band.hopSize) / config.sampleRate;

        bands[i] = band;
    }

    return bands;
}

std::size_t mtwPointCount(const MtwConfig& config) {
    validate(config);
    const std::size_t n0 = config.topFftSize;
    const std::size_t k = config.octaveCount;
    // Closed form, record §3 / conflict C2: top band (N0/2 - N0/8 + 1), K-1
    // middle bands at N0/8 each, bottom band N0/4 (DC up to its edge).
    return (n0 / 2 - n0 / 8 + 1) + (k - 1) * (n0 / 8) + (n0 / 4);
}

std::vector<double> mtwFrequencies(const MtwConfig& config) {
    const auto bands = mtwBands(config);
    std::vector<double> freq;
    freq.reserve(mtwPointCount(config));
    for (const auto& band : bands) {
        for (std::size_t bin = band.firstBin; bin <= band.lastBin; ++bin) {
            freq.push_back(static_cast<double>(bin) * config.sampleRate /
                            static_cast<double>(band.fftSize));
        }
    }
    return freq;
}

double mtwAlpha(const MtwConfig& config, std::size_t /*band*/) noexcept {
    // See the header: this cancels DualFftEngine's own hop_k/fs division, so
    // it is the SAME expression for every band, not seven that happen to
    // agree.
    return 1.0 - std::exp(-1.0 / config.timeConstantFrames);
}

double mtwIntegrationSeconds(const MtwConfig& config, std::size_t band) {
    const auto bands = mtwBands(config);
    if (band >= bands.size()) {
        throw std::invalid_argument("mtwIntegrationSeconds: band index out of range");
    }
    return bands[band].integrationSeconds;
}

}  // namespace rta::dsp
