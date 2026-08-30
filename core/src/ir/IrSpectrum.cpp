// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/ir/IrSpectrum.h"

#include "rta/dsp/RealFft.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rta::ir {
namespace {

// Duplicated from Deconvolver.cpp rather than shared through a header: five
// lines, no state, and the two files stay independently readable. If a third
// user appears, promote it then.
std::size_t nextPowerOfTwo(std::size_t n) {
    std::size_t size = 4;               // RealFft's documented minimum
    while (size < n) size <<= 1;
    return size;
}

constexpr double kTwoPi = 6.28318530717958647692528676656;

}  // namespace

std::size_t leadInSamplesFor(double sampleRate, double lowBandEdgeHz) {
    if (!(sampleRate > 0.0))
        throw std::invalid_argument("leadInSamplesFor: sampleRate must be positive");
    if (!(lowBandEdgeHz > 0.0))
        throw std::invalid_argument("leadInSamplesFor: lowBandEdgeHz must be positive");
    return static_cast<std::size_t>(std::llround(2.0 * sampleRate / lowBandEdgeHz));
}

Spectrum analyseSpectrum(const Deconvolution& source, const IrSpectrumConfig& config) {
    if (source.samples.empty())
        throw std::invalid_argument("analyseSpectrum: the deconvolution is empty");
    if (!(source.sampleRate > 0.0))
        throw std::invalid_argument("analyseSpectrum: sampleRate must be positive");

    const std::size_t leadIn = leadInSamplesFor(source.sampleRate, config.lowBandEdgeHz);
    if (leadIn > source.originIndex)
        throw std::invalid_argument(
            "analyseSpectrum: the deconvolution has less pre-arrival room than two "
            "cycles of the low band edge");

    const std::size_t start = source.originIndex - leadIn;
    const std::size_t length = source.samples.size() - start;
    const std::size_t fftSize = nextPowerOfTwo(length);

    dsp::RealFft fft(fftSize);
    std::vector<float> padded(fftSize, 0.0f);
    std::copy(source.samples.begin() + static_cast<std::ptrdiff_t>(start),
              source.samples.end(), padded.begin());

    Spectrum out;
    out.bins.resize(fft.numBins());
    fft.forward(padded, out.bins);
    out.binHz = source.sampleRate / static_cast<double>(fftSize);
    out.leadInSamples = leadIn;

    // Undo the lead-in's linear phase, so the caller reads phase referenced to
    // t = 0. Left in, it is a constant group delay of `leadIn` samples -- 5 ms
    // at a 400 Hz band edge -- biasing every downstream delay reading by an
    // amount that would be very hard to trace back to a windowing choice.
    //
    // The window began `leadIn` samples EARLY, so the transform sees everything
    // as arriving late by that much: advancing by exp(+i*w*leadIn) removes it.
    for (std::size_t k = 0; k < out.bins.size(); ++k) {
        const double theta = kTwoPi * static_cast<double>(k)
                           * static_cast<double>(leadIn) / static_cast<double>(fftSize);
        const auto rotation = std::complex<float>(static_cast<float>(std::cos(theta)),
                                                  static_cast<float>(std::sin(theta)));
        out.bins[k] *= rotation;
    }
    return out;
}

}  // namespace rta::ir
