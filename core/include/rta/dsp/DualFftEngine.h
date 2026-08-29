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

/// How frame PAIRS are combined. Deliberately NOT `Averaging`: that enum's
/// `Linear` means "every frame since reset, unbounded", which a live transfer
/// function must not do -- it would take minutes to forget a moved microphone.
enum class TransferAveraging {
    Fifo,         ///< running sum over the last `fifoDepth` frames, exact drop-off
    Exponential,  ///< one-pole, the coefficient SpectrumEngine already uses
};

/// Two ring buffers, one delay compensation, three running accumulators
/// (`Sxx`, `Syy`, `Sxy`). Everything an H1/H2/Hv estimator or a coherence
/// needs, and nothing else -- the estimators themselves live in
/// TransferEstimator.h and are free functions over these accumulators.
///
/// See docs/dsp/2026-08-28-dual-fft.md for the reasoning behind every
/// decision this class embodies. Not thread-safe; one instance per analysis
/// thread, same as SpectrumEngine.
class DualFftEngine {
public:
    struct Config {
        std::size_t fftSize = 4096;          ///< power of two, >= 4
        std::size_t hopSize = 2048;          ///< 1..fftSize
        double      sampleRate = 48000.0;
        WindowType  window = WindowType::Hann;
        TransferAveraging averaging = TransferAveraging::Fifo;
        std::size_t fifoDepth = 16;          ///< Fifo only, >= 1
        double timeConstantSeconds = 0.5;    ///< Exponential only, > 0

        /// Positive = the measurement lags the reference by this many samples.
        /// Applied BEFORE the transform, as an integer-sample stream offset --
        /// never afterwards to the phase. A delay comparable to the frame
        /// length decorrelates the two channels and kills coherence at HIGH
        /// frequency first, because HF has the shortest period; compensate
        /// after the FFT and the coherence on screen measures your own
        /// misalignment instead of the system. See the decision record §4.
        int referenceDelaySamples = 0;

        /// Coherence is withheld below this many EFFECTIVE averages (task 1).
        /// For a single frame |X*Y|^2 == |X|^2|Y|^2 identically, so coherence
        /// is exactly 1.0 at every frequency and a broken engine looks perfect.
        double minimumEffectiveAverages = 8.0;
    };

    explicit DualFftEngine(const Config& config);

    [[nodiscard]] const Config& config() const noexcept { return config_; }
    [[nodiscard]] std::size_t numBins() const noexcept { return numBins_; }
    [[nodiscard]] std::size_t frameCount() const noexcept { return frameCount_; }
    /// Never a raw frame count -- see AverageCount.h.
    [[nodiscard]] double effectiveAverages() const noexcept;
    [[nodiscard]] double binWidthHz() const noexcept;
    [[nodiscard]] double binFrequency(std::size_t bin) const noexcept;

    /// Both spans must be the same length; they are the SAME time instant on
    /// two channels. Throws std::invalid_argument otherwise -- silently
    /// analysing two channels that are not the same instant is the one error
    /// this class must never make quietly.
    void process(std::span<const float> reference, std::span<const float> measurement);

    void reset() noexcept;

    /// Averaged one-sided PSDs and cross-PSD. Doubles: the estimators divide
    /// them, and a float cross-spectrum loses phase precision where |Sxx| is
    /// small, which is exactly where the operator is looking.
    [[nodiscard]] std::span<const double> referencePsd() const noexcept { return sxxMean_; }    ///< Sxx
    [[nodiscard]] std::span<const double> measurementPsd() const noexcept { return syyMean_; }  ///< Syy
    [[nodiscard]] std::span<const std::complex<double>> crossPsd() const noexcept { return sxyMean_; }  ///< Sxy

private:
    void validate() const;
    void armDelaySkip() noexcept;
    void drainFrames();
    void analyseFramePair();
    void accumulate(std::size_t bin, double sxx, double syy, std::complex<double> sxy) noexcept;

    Config  config_;
    Window  window_;
    RealFft fft_;
    std::size_t numBins_ = 0;

    double densityScale_ = 0.0;  ///< PsdScaling::scale(fs, sum(w^2))
    double alpha_ = 0.0;         ///< Exponential smoothing coefficient

    RingBuffer<float> referenceRing_;
    RingBuffer<float> measurementRing_;

    /// One-time, construction-only stream offsets (Config::referenceDelaySamples,
    /// implementation note 1). Exactly one of these is non-zero at any time;
    /// both count down to zero once and never move again until reset().
    std::size_t referenceSkip_ = 0;
    std::size_t measurementSkip_ = 0;

    std::vector<float> referenceFrame_;
    std::vector<float> measurementFrame_;
    std::vector<float> referenceWindowed_;
    std::vector<float> measurementWindowed_;
    std::vector<std::complex<float>> referenceBins_;
    std::vector<std::complex<float>> measurementBins_;

    /// FIFO storage: `fifoDepth * numBins` slots per accumulator, indexed
    /// `[slot * numBins + bin]`. Only used when config_.averaging == Fifo, but
    /// allocated unconditionally at construction so process() never allocates.
    std::vector<double> fifoSxx_;
    std::vector<double> fifoSyy_;
    std::vector<std::complex<double>> fifoSxy_;
    std::vector<double> runningSumSxx_;
    std::vector<double> runningSumSyy_;
    std::vector<std::complex<double>> runningSumSxy_;

    /// The public output: the current mean, whichever averaging mode fills it.
    std::vector<double> sxxMean_;
    std::vector<double> syyMean_;
    std::vector<std::complex<double>> sxyMean_;

    std::size_t frameCount_ = 0;
};

}  // namespace rta::dsp
