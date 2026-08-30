// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/ir/Deconvolver.h"

#include "rta/dsp/RealFft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>

namespace rta::ir {
namespace {

std::size_t nextPowerOfTwo(std::size_t n) {
    std::size_t size = 4;               // RealFft's documented minimum
    while (size < n) size <<= 1;
    return size;
}

/// Magnitudes of a Deconvolution's spectrum, plus the bin spacing in hertz.
struct Magnitudes {
    std::vector<float> values;
    double binHz = 0.0;
};

Magnitudes magnitudesOf(const Deconvolution& source) {
    const std::size_t fftSize = nextPowerOfTwo(source.samples.size());
    dsp::RealFft fft(fftSize);
    std::vector<float> padded(fftSize, 0.0f);
    std::copy(source.samples.begin(), source.samples.end(), padded.begin());
    std::vector<std::complex<float>> bins(fft.numBins());
    fft.forward(padded, bins);

    Magnitudes out;
    out.values.resize(bins.size());
    for (std::size_t k = 0; k < bins.size(); ++k)
        out.values[k] = std::abs(bins[k]);
    out.binHz = source.sampleRate / static_cast<double>(fftSize);
    return out;
}

void requireBand(const Deconvolution& source, double lowHz, double highHz) {
    if (source.samples.empty())
        throw std::invalid_argument("band query: the deconvolution is empty");
    if (!(source.sampleRate > 0.0))
        throw std::invalid_argument("band query: sampleRate must be positive");
    if (!(lowHz > 0.0) || !(highHz > lowHz))
        throw std::invalid_argument("band query: need 0 < lowHz < highHz");
}

/// Sum of in-band magnitudes and how many bins contributed. Shared so the two
/// public band queries cannot disagree about which bins are "in band" -- a
/// disagreement that would make a normalisation and the flatness measured
/// against it describe different bands.
struct BandSum {
    double sum = 0.0;
    std::size_t count = 0;
};

BandSum sumInBand(const Magnitudes& mag, double lowHz, double highHz) {
    BandSum out;
    for (std::size_t k = 0; k < mag.values.size(); ++k) {
        const double hz = static_cast<double>(k) * mag.binHz;
        if (hz < lowHz || hz > highHz) continue;
        out.sum += mag.values[k];
        ++out.count;
    }
    return out;
}

}  // namespace

double Deconvolution::harmonicOffsetSamples(int order) const noexcept {
    // dt_N = -L*ln(N). The Nth harmonic's instantaneous frequency at time t is
    // the fundamental's at t + L*ln(N), and deconvolution maps the fundamental
    // at t onto zero -- so it maps that harmonic onto -L*ln(N). Order 1 is the
    // fundamental, which is the origin itself, so its offset is zero.
    if (order <= 1) return 0.0;
    return -harmonicSpacingL * std::log(static_cast<double>(order)) * sampleRate;
}

Deconvolution deconvolve(std::span<const float> response,
                         std::span<const float> inverseFilter,
                         const DeconvolverConfig& config) {
    if (inverseFilter.empty())
        throw std::invalid_argument("deconvolve: inverseFilter is empty");
    if (response.empty())
        throw std::invalid_argument("deconvolve: response is empty");
    if (!(config.sampleRate > 0.0))
        throw std::invalid_argument("deconvolve: sampleRate must be positive");
    if (response.size() < inverseFilter.size())
        throw std::invalid_argument(
            "deconvolve: response is shorter than the inverse filter, so t=0 "
            "would fall outside the result");

    const std::size_t linearLength = response.size() + inverseFilter.size() - 1;
    const std::size_t fftSize = nextPowerOfTwo(linearLength);

    dsp::RealFft fft(fftSize);
    std::vector<float> padded(fftSize, 0.0f);
    std::vector<std::complex<float>> spectrum(fft.numBins());
    std::vector<std::complex<float>> filter(fft.numBins());

    std::copy(response.begin(), response.end(), padded.begin());
    fft.forward(padded, spectrum);

    std::fill(padded.begin(), padded.end(), 0.0f);
    std::copy(inverseFilter.begin(), inverseFilter.end(), padded.begin());
    fft.forward(padded, filter);

    // Multiplication in the frequency domain is convolution in time. The
    // transform is longer than `linearLength`, so the tail that would have
    // wrapped lands in the zero padding instead -- this is a LINEAR
    // convolution, which is what pulls the harmonic packets to negative times
    // where they can be told apart from the impulse response.
    for (std::size_t k = 0; k < spectrum.size(); ++k) spectrum[k] *= filter[k];
    fft.inverse(spectrum, padded);

    Deconvolution out;
    out.samples.assign(padded.begin(), padded.begin() + static_cast<std::ptrdiff_t>(linearLength));
    if (config.normalisationGain != 1.0) {
        const float gain = static_cast<float>(config.normalisationGain);
        for (auto& sample : out.samples) sample *= gain;
    }

    // inv[m] = s[Ns-1-m]*e[m], so at lag Ns-1 every term of the convolution sum
    // becomes s[j]^2 * e[Ns-1-j] -- all non-negative, the one lag at which the
    // sum adds coherently. Ninv == Ns, hence Ninv-1.
    out.originIndex = inverseFilter.size() - 1;
    out.sampleRate = config.sampleRate;
    out.normalisationGain = config.normalisationGain;
    out.harmonicSpacingL = config.harmonicSpacingL;
    return out;
}

double inBandNormalisation(const Deconvolution& reference, double lowHz, double highHz) {
    requireBand(reference, lowHz, highHz);
    const auto mag = magnitudesOf(reference);
    const auto band = sumInBand(mag, lowHz, highHz);
    if (band.count == 0)
        throw std::invalid_argument("inBandNormalisation: no bins inside the band");
    if (!(band.sum > 0.0))
        throw std::invalid_argument("inBandNormalisation: the band holds no energy");
    return static_cast<double>(band.count) / band.sum;
}

BandFlatness bandFlatness(const Deconvolution& reference, double lowHz, double highHz) {
    requireBand(reference, lowHz, highHz);
    const auto mag = magnitudesOf(reference);
    const auto band = sumInBand(mag, lowHz, highHz);
    if (band.count == 0)
        throw std::invalid_argument("bandFlatness: no bins inside the band");
    if (!(band.sum > 0.0))
        throw std::invalid_argument("bandFlatness: the band holds no energy");
    const double mean = band.sum / static_cast<double>(band.count);

    BandFlatness out{ 0.0, 0.0 };
    bool first = true;
    for (std::size_t k = 0; k < mag.values.size(); ++k) {
        const double hz = static_cast<double>(k) * mag.binHz;
        if (hz < lowHz || hz > highHz) continue;
        // Floor the ratio rather than let a zero bin produce -inf: a single
        // exactly-zero bin would otherwise swamp the reported range with a
        // number that says nothing about the band's shape.
        const double db = 20.0 * std::log10(std::max(1.0e-30,
                              static_cast<double>(mag.values[k]) / mean));
        if (first) { out.minDb = out.maxDb = db; first = false; }
        else       { out.minDb = std::min(out.minDb, db);
                     out.maxDb = std::max(out.maxDb, db); }
    }
    return out;
}

}  // namespace rta::ir
