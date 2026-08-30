// SPDX-License-Identifier: AGPL-3.0-or-later
// Decision 6b of docs/dsp/2026-08-30-sweep-ir-l4a.md.

#include "rta/ir/Polarity.h"

#include "rta/dsp/RealFft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <vector>

namespace rta::ir {
namespace {

std::size_t nextPowerOfTwo(std::size_t n) {
    std::size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

struct Edges {
    double lowHz = 0.0;
    double highHz = 0.0;
    bool valid = false;
    /// The window could not be sized: the capture ends before ten cycles of the
    /// measured low edge. Distinct from `!valid`, because the caller must say so
    /// rather than report an edge it does not have.
    bool tooShort = false;
};

/// Where between two bins the magnitude crosses `thresholdDb`, in hertz.
///
/// Taking the bin index itself charges up to a full bin of quantisation to a
/// threshold expressed in hertz -- near 100 Hz with 11.7 Hz bins that is
/// 0.16 octave of pure estimator noise. Linear in dB against linear in
/// frequency, which is what the measured -10 dB skirt looks like locally.
double interpolatedCrossing(const std::vector<double>& magDb, std::size_t k,
                            std::size_t kNext, double binHz, double thresholdDb) {
    const double y0 = magDb[k];
    const double y1 = magDb[kNext];
    if (y1 == y0) return static_cast<double>(k) * binHz;
    double t = (thresholdDb - y0) / (y1 - y0);
    t = std::clamp(t, 0.0, 1.0);
    return (static_cast<double>(k) + t * (static_cast<double>(kNext) - static_cast<double>(k)))
           * binHz;
}

/// The -10 dB edges of the arrival's own spectrum.
///
/// Three defects of the obvious implementation are corrected here, each
/// measured; see decision 6b. In short: a fixed window cannot resolve a low
/// band (a 50-71 Hz subwoofer read 46.9 Hz - 18270 Hz, wrong by about 90 dB, and
/// landed inside the gate built to refuse it); bins below the excitation carry
/// leakage rather than response; and integer bin indices quantise the answer.
///
/// Recurses ONCE. The second window only ever grows, and a longer window cannot
/// push the low edge higher, so a further pass would chase its own tail.
Edges bandEdges(const Deconvolution& source, std::size_t from, std::size_t to,
                double lowClampHz, double highClampHz, double cycles = 10.0,
                bool secondPass = false) {
    if (to <= from || to > source.samples.size()) return {};
    const std::size_t span = to - from;
    const std::size_t fftSize = nextPowerOfTwo(span);
    if (fftSize < 2) return {};

    dsp::RealFft fft(fftSize);
    std::vector<float> padded(fftSize, 0.0f);
    std::copy(source.samples.begin() + static_cast<std::ptrdiff_t>(from),
              source.samples.begin() + static_cast<std::ptrdiff_t>(to), padded.begin());
    std::vector<std::complex<float>> bins(fft.numBins());
    fft.forward(padded, bins);

    const double binHz = source.sampleRate / static_cast<double>(fftSize);

    // Clamp to the band the excitation covered. Zero means "not stated", and a
    // reader must then not clamp rather than assume a full band.
    const double lowLimit = lowClampHz > 0.0 ? lowClampHz : 0.0;
    const double highLimit = highClampHz > 0.0 ? highClampHz : source.sampleRate;

    std::size_t firstBin = bins.size(), lastBin = 0;
    double peak = 0.0;
    for (std::size_t k = 1; k < bins.size(); ++k) {
        const double hz = static_cast<double>(k) * binHz;
        if (hz < lowLimit || hz > highLimit) continue;
        firstBin = std::min(firstBin, k);
        lastBin = std::max(lastBin, k);
        peak = std::max(peak, static_cast<double>(std::abs(bins[k])));
    }
    if (firstBin > lastBin || peak <= 0.0) return {};

    std::vector<double> magDb(bins.size(), -300.0);
    for (std::size_t k = 0; k < bins.size(); ++k) {
        const double m = static_cast<double>(std::abs(bins[k]));
        magDb[k] = 20.0 * std::log10(std::max(m, 1e-30) / peak);
    }

    std::size_t lowBin = 0, highBin = 0;
    bool found = false;
    for (std::size_t k = firstBin; k <= lastBin; ++k) {
        if (magDb[k] < -10.0) continue;
        if (!found) { lowBin = k; found = true; }
        highBin = k;
    }
    if (!found || highBin < lowBin) return {};

    if (!secondPass) {
        const double roughLowHz = std::max(static_cast<double>(lowBin) * binHz, 1.0);
        const auto needed =
            static_cast<std::size_t>(std::ceil(cycles * source.sampleRate / roughLowHz));
        if (needed > span) {
            if (from + needed <= source.samples.size())
                return bandEdges(source, from, from + needed, lowClampHz, highClampHz,
                                 cycles, true);
            // The capture ends first. Do NOT fall through to the first-pass
            // reading: that reading is the fiction this whole function exists to
            // remove, and returning it silently is how a fixed defect returns.
            Edges tooShort;
            tooShort.tooShort = true;
            return tooShort;
        }
    }

    Edges out;
    out.lowHz = lowBin > firstBin
                    ? interpolatedCrossing(magDb, lowBin, lowBin - 1, binHz, -10.0)
                    : static_cast<double>(lowBin) * binHz;
    out.highHz = highBin < lastBin
                     ? interpolatedCrossing(magDb, highBin, highBin + 1, binHz, -10.0)
                     : static_cast<double>(highBin) * binHz;
    out.valid = true;
    return out;
}

}  // namespace

PolarityResult findPolarity(const Deconvolution& source, const PolarityConfig& config) {
    if (!(source.sampleRate > 0.0))
        throw std::invalid_argument("findPolarity: sampleRate must be positive");
    if (!(config.arrivalFraction > 0.0) || !(config.arrivalFraction <= 1.0))
        throw std::invalid_argument("findPolarity: arrivalFraction must be in (0, 1]");

    PolarityResult out;

    // Before measuring anything: can a sweep like this one answer the question
    // at all? Its trusted band must reach both gate thresholds, or every
    // loudspeaker it measures will fail the gate for a reason that belongs to
    // the excitation. Zero means "not stated", and an unstated band cannot
    // refuse anything.
    if ((source.trustedLowHz > 0.0 && source.trustedLowHz > config.gateLowHz)
        || (source.trustedHighHz > 0.0 && source.trustedHighHz < config.gateHighHz)) {
        out.refusal = Refusal::SweepBandInsufficient;
        return out;
    }

    const auto span =
        static_cast<std::size_t>(std::llround(config.searchSeconds * source.sampleRate));
    const std::size_t last = std::min(source.originIndex + span, source.samples.size());
    if (last <= source.originIndex) return out;                     // NoSignal

    double peak = 0.0;
    for (std::size_t i = source.originIndex; i < last; ++i)
        peak = std::max(peak, std::abs(static_cast<double>(source.samples[i])));
    if (peak <= 0.0) return out;                                    // NoSignal

    const auto edges = bandEdges(source, source.originIndex, last,
                                 source.excitationLowHz, source.excitationHighHz);
    if (edges.tooShort) {
        out.refusal = Refusal::CaptureTooShort;
        return out;
    }
    out.lowEdgeHz = edges.lowHz;
    out.highEdgeHz = edges.highHz;

    // The noise window runs from the second harmonic packet to the origin. H2 is
    // the first packet and sits at exactly -L*ln(2) by decision 3's closed form,
    // so the left edge is computed rather than chosen. What it contains is the
    // PRE-ARRIVAL FLOOR -- noise, sidelobes, and residual harmonic energy, since
    // the edge sits on H2 itself. That is the right denominator operationally: a
    // distorting system should read as less trustworthy.
    const auto h2 = static_cast<std::size_t>(
        std::llround(-source.harmonicOffsetSamples(2)));
    if (h2 == 0 || h2 > source.originIndex) {
        // harmonicSpacingL was not supplied. deconvolve() accepts that and never
        // validates it, so this is a legal input and must not throw.
        out.refusal = Refusal::NoNoiseEstimate;
        return out;
    }
    const std::size_t noiseFrom = source.originIndex - h2;
    double sumSq = 0.0;
    std::size_t count = 0;
    for (std::size_t i = noiseFrom; i < source.originIndex; ++i) {
        const double v = static_cast<double>(source.samples[i]);
        sumSq += v * v;
        ++count;
    }
    if (count == 0) {
        out.refusal = Refusal::NoNoiseEstimate;
        return out;
    }
    const double noiseRms = std::sqrt(sumSq / static_cast<double>(count));
    out.confidenceDb = noiseRms > 0.0 ? 20.0 * std::log10(peak / noiseRms) : 0.0;

    // The verdict: the first excursion reaching `arrivalFraction` of the peak.
    Sign verdict = Sign::Unknown;
    for (std::size_t i = source.originIndex; i < last; ++i) {
        if (std::abs(static_cast<double>(source.samples[i]))
            >= config.arrivalFraction * peak) {
            out.arrivalIndex = i;
            verdict = source.samples[i] > 0.0f ? Sign::Positive : Sign::Negative;
            break;
        }
    }
    if (verdict == Sign::Unknown) return out;                       // NoSignal

    // Ambiguity, measured against the VERDICT's sign rather than the window
    // peak's -- so a window whose largest excursion disagrees with the answer
    // reports a margin above 1 instead of hiding it at exactly 1.
    double opposite = 0.0;
    for (std::size_t i = source.originIndex; i < last; ++i) {
        const double v = static_cast<double>(source.samples[i]);
        const bool disagrees = verdict == Sign::Positive ? v < 0.0 : v > 0.0;
        if (disagrees) opposite = std::max(opposite, std::abs(v));
    }
    out.margin = opposite / peak;

    // Structural refusals win over NoSignal: measuring louder cannot widen a
    // band, and these are the two that tell the operator where to go instead.
    if (!edges.valid) {
        // Nothing measurable inside the excitation band. That is an absent
        // measurement, not a narrow one -- calling it BandTooLow would tell the
        // operator "this is a horn" about a spectrum nobody could read.
        out.refusal = Refusal::NoSignal;
        return out;
    }
    if (out.lowEdgeHz > config.gateLowHz) {
        out.refusal = Refusal::BandTooLow;
        return out;
    }
    if (out.highEdgeHz < config.gateHighHz) {
        out.refusal = Refusal::BandTooHigh;
        return out;
    }
    if (out.confidenceDb < config.minConfidenceDb) {
        out.refusal = Refusal::NoSignal;
        return out;
    }

    out.sign = verdict;
    out.refusal = Refusal::None;
    return out;
}

}  // namespace rta::ir
