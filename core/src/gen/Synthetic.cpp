// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/gen/Synthetic.h"

#include "rta/dsp/RealFft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <stdexcept>

namespace rta::gen {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

// dBFS convention fixed by plan docs/plans/2026-08-27-audioio-rta-impl-plan.md
// §1.4: a full-scale SINE reads 0.0 dB. The identical constant is defined
// again in app/src/measure/Levels.h -- core must not depend on app/ (the "one
// rule that governs the architecture" in CLAUDE.md), so the two definitions
// are independent and must be kept in sync by hand, not shared. 10*log10(2)
// exactly: a unit-amplitude sine has mean square 0.5, and
// 10*log10(0.5) + 3.0103 = 0.0.
constexpr double kFullScaleSineOffsetDb = 3.0102999566398120;

double powerFromDbFs(const double levelDbFs) {
    return std::pow(10.0, (levelDbFs - kFullScaleSineOffsetDb) / 10.0);
}

// std::mt19937 is specified bit-exactly by the standard; the distributions in
// <random> are not, so a snapshot built with std::uniform_real_distribution
// would differ between MSVC and libstdc++ even for the same seed and the same
// engine draws. Map the engine's raw output to [0,1) by hand instead.
double nextUnitInterval(std::mt19937& rng) {
    constexpr double kRange = double(std::mt19937::max()) - double(std::mt19937::min()) + 1.0;
    return (double(rng()) - double(std::mt19937::min())) / kRange;
}

std::vector<float> renderPinkBlock(const std::size_t blockSize, const double levelDbFs,
                                    const std::uint32_t seed) {
    dsp::RealFft fft(blockSize);  // throws std::invalid_argument if not a power of two >= 4
    const std::size_t numBins = fft.numBins();

    std::mt19937 rng(seed);
    std::vector<std::complex<float>> spectrum(numBins);
    spectrum[0] = { 0.0f, 0.0f };  // DC: pink noise has no level defined at 0 Hz
    for (std::size_t k = 1; k < numBins; ++k) {
        // |X(k)| ~ k^-1/2 makes the power spectrum exactly 1/k, i.e. exactly
        // -3.0103 dB per doubling of k -- and doubling k is doubling frequency,
        // because the bin-to-hertz mapping is linear. Exact by construction,
        // never fitted or filtered.
        const double magnitude = 1.0 / std::sqrt(double(k));
        const double phase = nextUnitInterval(rng) * kTwoPi;
        spectrum[k] = { float(magnitude * std::cos(phase)), float(magnitude * std::sin(phase)) };
    }

    std::vector<float> block(blockSize);
    fft.inverse(spectrum, block);

    // Normalise to the requested RMS-referenced dBFS level. The window over
    // which RMS is measured is the whole loop block, so the level a caller
    // asked for is exact once the block has been read at least once through.
    double sumSquares = 0.0;
    for (const float sample : block) sumSquares += double(sample) * double(sample);
    const double rms = std::sqrt(sumSquares / double(block.size()));
    const double targetRms = std::sqrt(powerFromDbFs(levelDbFs));
    const float scale = rms > 0.0 ? float(targetRms / rms) : 0.0f;
    for (float& sample : block) sample *= scale;

    return block;
}

double validatedFrequency(const double sampleRateHz, const double frequencyHz) {
    if (!(sampleRateHz > 0.0) || !(frequencyHz > 0.0) || !(frequencyHz < sampleRateHz / 2.0)) {
        throw std::invalid_argument("SyntheticSine needs 0 < frequencyHz < sampleRateHz / 2");
    }
    return frequencyHz;
}

}  // namespace

SyntheticPink::SyntheticPink(const std::size_t blockSize, const double levelDbFs,
                              const std::uint32_t seed)
    : block_(renderPinkBlock(blockSize, levelDbFs, seed)) {}

void SyntheticPink::render(std::span<float> out) noexcept {
    std::size_t written = 0;
    while (written < out.size()) {
        const std::size_t remaining = block_.size() - position_;
        const std::size_t n = std::min(remaining, out.size() - written);
        std::copy_n(block_.data() + position_, n, out.data() + written);
        position_ += n;
        written += n;
        if (position_ == block_.size()) position_ = 0;
    }
}

void SyntheticPink::reset() noexcept { position_ = 0; }

SyntheticSine::SyntheticSine(const double sampleRateHz, const double frequencyHz,
                              const double levelDbFs)
    : phaseIncrement_(kTwoPi * validatedFrequency(sampleRateHz, frequencyHz) / sampleRateHz),
      amplitude_(std::sqrt(2.0 * powerFromDbFs(levelDbFs))) {}

void SyntheticSine::render(std::span<float> out) noexcept {
    for (float& sample : out) {
        sample = float(amplitude_ * std::sin(phase_));
        phase_ += phaseIncrement_;
        if (phase_ >= kTwoPi) phase_ -= kTwoPi;
    }
}

void SyntheticSine::reset() noexcept { phase_ = 0.0; }

}  // namespace rta::gen
