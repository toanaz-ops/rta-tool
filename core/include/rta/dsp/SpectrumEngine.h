// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/dsp/RealFft.h"
#include "rta/dsp/RingBuffer.h"
#include "rta/dsp/Window.h"

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

namespace rta::dsp {

/// How successive frames are combined.
enum class Averaging {
    /// Equal weight to every frame since the last reset. This is what
    /// scipy.signal.welch does, and what you want for a settled measurement of
    /// something that is not changing.
    Linear,
    /// One-pole smoothing towards each new frame. Never settles, always
    /// current -- the mode an engineer actually watches while moving a fader.
    Exponential,
};

/// Welch spectrum estimation: overlapping windowed frames, transformed and
/// averaged.
///
/// ## The two outputs are not interchangeable
///
/// `spectrum()` is the power spectrum: a sine sitting on a bin centre reads its
/// true mean square there. `density()` is the power spectral density, per hertz.
/// They differ by exactly the window's equivalent noise bandwidth.
///
/// **A frequency band is summed from `density()`.** Summing `spectrum()` over a
/// band over-counts broadband energy by that same ENBW, because the window
/// correlates neighbouring bins -- with Hann that is 1.76 dB high on every noise
/// or music source, on a plot that looks entirely reasonable. See
/// docs/dsp/2026-08-26-banding-and-averaging.md.
///
/// Not thread-safe. One instance per analysis thread.
class SpectrumEngine {
public:
    struct Config {
        std::size_t fftSize = 4096;       ///< power of two, >= 4
        std::size_t hopSize = 2048;       ///< samples advanced per frame, 1..fftSize
        double      sampleRate = 48000.0;
        WindowType  window = WindowType::Hann;
        Averaging   averaging = Averaging::Linear;
        double      timeConstantSeconds = 0.5;  ///< Exponential only
    };

    explicit SpectrumEngine(const Config& config);

    [[nodiscard]] const Config& config() const noexcept { return config_; }
    [[nodiscard]] std::size_t numBins() const noexcept { return fft_.numBins(); }
    [[nodiscard]] std::size_t frameCount() const noexcept { return frameCount_; }

    [[nodiscard]] double binWidthHz() const noexcept;
    [[nodiscard]] double binFrequency(std::size_t bin) const noexcept;

    /// fs * sum(w^2) / sum(w)^2. The width of the ideal rectangular filter that
    /// would pass the same noise power as one windowed bin.
    [[nodiscard]] double equivalentNoiseBandwidthHz() const noexcept { return enbwHz_; }

    /// Consume samples. Frames are produced whenever enough have accumulated,
    /// so callers may pass whatever block size the driver gave them; a trailing
    /// partial frame is held until the rest of it arrives.
    void process(std::span<const float> samples);

    /// Discard the average and all buffered samples.
    void reset() noexcept;

    /// Power spectral density, units^2 per hertz. Sum this over a band.
    [[nodiscard]] std::span<const float> density() const noexcept { return density_; }

    /// Power spectrum, mean square per bin. Read a tone from this.
    [[nodiscard]] std::span<const float> spectrum() const noexcept { return spectrum_; }

private:
    void drainFrames();
    void analyseFrame();

    Config      config_;
    Window      window_;
    RealFft     fft_;

    double densityScale_ = 0.0;  ///< 2 / (fs * sum(w^2))
    double enbwHz_ = 0.0;
    double alpha_ = 0.0;         ///< Exponential smoothing coefficient

    RingBuffer<float>                pending_;
    std::vector<float>               frame_;
    std::vector<float>               windowed_;
    std::vector<std::complex<float>> bins_;

    std::vector<double> accumulator_;  ///< running mean, in double
    std::vector<float>  density_;
    std::vector<float>  spectrum_;

    std::size_t frameCount_ = 0;
};

}  // namespace rta::dsp
