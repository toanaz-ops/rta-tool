// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/SpectrumEngine.h"

#include "rta/dsp/PsdScaling.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace rta::dsp {

namespace {

/// The accumulator holds enough samples for one frame plus room for whatever
/// block the caller hands over. Four frames is generous without being
/// expensive, and it means a caller passing an unusually large block does not
/// force the loop in process() to interleave.
std::size_t bufferCapacity(const std::size_t fftSize) {
    return std::bit_ceil(std::max<std::size_t>(fftSize * 4, 4096));
}

}  // namespace

SpectrumEngine::SpectrumEngine(const Config& config)
    : config_(config),
      window_(config.window, config.fftSize),
      fft_(config.fftSize),
      pending_(bufferCapacity(config.fftSize)) {
    if (config.hopSize == 0 || config.hopSize > config.fftSize) {
        throw std::invalid_argument("SpectrumEngine hopSize must be within 1..fftSize");
    }
    if (!(config.sampleRate > 0.0)) {
        throw std::invalid_argument("SpectrumEngine sampleRate must be positive");
    }
    if (config.averaging == Averaging::Exponential && !(config.timeConstantSeconds > 0.0)) {
        throw std::invalid_argument("SpectrumEngine timeConstantSeconds must be positive");
    }

    // PSD[k] = 2 |X[k]|^2 / (fs * sum(w^2)); DC and Nyquist lose the factor of
    // two because they have no mirror-image partner to fold in. The single
    // definition of this lives in PsdScaling.h so DualFftEngine cannot drift
    // from it into a divergent dB offset -- see docs/dsp/2026-08-28-dual-fft.md §5.
    densityScale_ = PsdScaling::scale(config.sampleRate, window_.sumSquares());
    enbwHz_ = config.sampleRate * window_.sumSquares() / (window_.sum() * window_.sum());

    // One frame advances the clock by hopSize samples, so that is the interval
    // the time constant is measured against -- not the frame length, which
    // overlaps its neighbours and would double-count the elapsed time.
    const double hopSeconds = static_cast<double>(config.hopSize) / config.sampleRate;
    alpha_ = 1.0 - std::exp(-hopSeconds / config.timeConstantSeconds);

    frame_.resize(config.fftSize);
    windowed_.resize(config.fftSize);
    bins_.resize(fft_.numBins());
    accumulator_.assign(fft_.numBins(), 0.0);
    density_.assign(fft_.numBins(), 0.0f);
    spectrum_.assign(fft_.numBins(), 0.0f);
}

double SpectrumEngine::binWidthHz() const noexcept {
    return config_.sampleRate / static_cast<double>(config_.fftSize);
}

double SpectrumEngine::binFrequency(const std::size_t bin) const noexcept {
    return static_cast<double>(bin) * binWidthHz();
}

void SpectrumEngine::process(std::span<const float> samples) {
    std::size_t offset = 0;

    while (offset < samples.size()) {
        const std::size_t room = pending_.availableToWrite();
        if (room == 0) {
            drainFrames();
            if (pending_.availableToWrite() == 0) {
                // Only reachable if the buffer is smaller than one frame, which
                // the constructor makes impossible. Bail rather than spin.
                throw std::logic_error("SpectrumEngine accumulator cannot drain");
            }
            continue;
        }

        const std::size_t count = std::min(room, samples.size() - offset);
        pending_.write(samples.subspan(offset, count));
        offset += count;
        drainFrames();
    }
}

void SpectrumEngine::drainFrames() {
    // A frame is emitted for every whole fftSize of samples available, and the
    // read position then advances by only hopSize -- so the next frame sees the
    // overlap again. A trailing remainder shorter than a frame stays in the
    // buffer until the rest of it arrives, which is how scipy.signal.welch
    // treats a ragged tail and how a live stream has to behave anyway.
    while (pending_.availableToRead() >= config_.fftSize) {
        // The loop condition guarantees a full frame, so a short read would
        // mean the buffer and its accounting disagree -- analysing a frame
        // padded with stale samples would produce a plausible spectrum of
        // something that was never measured.
        const std::size_t got = pending_.peek(frame_);
        if (got != config_.fftSize) {
            throw std::logic_error("SpectrumEngine read a short frame");
        }
        pending_.discard(config_.hopSize);
        analyseFrame();
    }
}

void SpectrumEngine::analyseFrame() {
    window_.apply(frame_, windowed_);
    fft_.forward(windowed_, bins_);

    ++frameCount_;

    for (std::size_t k = 0; k < bins_.size(); ++k) {
        const double magnitudeSquared = static_cast<double>(std::norm(bins_[k]));
        const double psd = magnitudeSquared * densityScale_ * PsdScaling::binFactor(k, bins_.size());

        if (frameCount_ == 1) {
            // Seed from the first frame rather than from zero. An exponential
            // average started at zero spends several time constants climbing
            // out of a hole that is not a measurement of anything.
            accumulator_[k] = psd;
        } else if (config_.averaging == Averaging::Linear) {
            accumulator_[k] += (psd - accumulator_[k]) / static_cast<double>(frameCount_);
        } else {
            accumulator_[k] += alpha_ * (psd - accumulator_[k]);
        }

        density_[k] = static_cast<float>(accumulator_[k]);
        spectrum_[k] = static_cast<float>(accumulator_[k] * enbwHz_);
    }
}

void SpectrumEngine::reset() noexcept {
    pending_.reset();
    std::fill(accumulator_.begin(), accumulator_.end(), 0.0);
    std::fill(density_.begin(), density_.end(), 0.0f);
    std::fill(spectrum_.begin(), spectrum_.end(), 0.0f);
    frameCount_ = 0;
}

}  // namespace rta::dsp
