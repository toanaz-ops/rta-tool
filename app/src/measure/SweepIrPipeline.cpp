// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/SweepIrPipeline.h"

#include "rta/ir/Deconvolver.h"
#include "rta/ir/IrSpectrum.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace rta::measure {

namespace {

// How far past the measurement's arrival the spectrum window must still
// reach. 0.1 s is the shortest IR a speaker plus a small room can be judged
// on at the bottom of the band; REW's own IR window for full-range work is
// 125 ms (research Part B, which edge is unverified). Fixed here, not a knob:
// a user-adjustable window is a gate, and the gate belongs to G25 (record
// sec. 7).
constexpr double kMinIrAfterArrivalSeconds = 0.1;

// A loop whose deconvolution has no pulse standing this far above its own rms
// is not a loop: silence, a disconnected cable, or noise. Gaussian noise over
// 10^6 samples peaks near sqrt(2 ln n) = 5.3 rms, so 20 leaves a factor 3.8
// of margin; a real sweep pulse is measured far above it (the test prints
// the figure).
constexpr double kMinPeakToRms = 20.0;

std::size_t nextPowerOfTwo(std::size_t n) {
    std::size_t size = 4;  // RealFft's documented minimum
    while (size < n) size <<= 1;
    return size;
}

struct Peak {
    std::size_t index = 0;
    float value = 0.0f;
    double peakToRms = 0.0;
};

// Largest |x| (first on ties), plus how far it stands above the rms.
Peak findPeak(const std::vector<float>& samples) {
    Peak peak;
    double sumSquares = 0.0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double x = samples[i];
        sumSquares += x * x;
        if (std::fabs(samples[i]) > std::fabs(peak.value)) {
            peak.index = i;
            peak.value = samples[i];
        }
    }
    const double count = static_cast<double>(std::max<std::size_t>(1, samples.size()));
    const double rms = std::sqrt(sumSquares / count);
    peak.peakToRms = rms > 0.0 ? std::fabs(static_cast<double>(peak.value)) / rms : 0.0;
    return peak;
}

SweepIrOutcome refuse(SweepIrStatus status, std::string message) {
    SweepIrOutcome out;
    out.status = status;
    out.message = std::move(message);
    return out;
}

SweepIrOutcome analyseChecked(const SweepCapture& capture, const SweepIrRequest& request) {
    const rta::gen::Sweep sweep(request.sweep);
    const std::size_t sweepSamples = sweep.lengthSamples();
    const double fs = request.sweep.sampleRate;
    const double fLo = sweep.validBandLowHz();
    const double fHi = sweep.validBandHighHz();

    if (capture.reference.size() != capture.measurement.size() ||
        capture.reference.size() < sweepSamples) {
        return refuse(SweepIrStatus::CaptureTooShort, "capture shorter than the sweep");
    }

    const std::vector<float> inverse = sweep.buildInverseFilter();

    rta::ir::DeconvolverConfig deconvConfig;
    deconvConfig.sampleRate = fs;
    deconvConfig.harmonicSpacingL = sweep.lengthConstantL();
    deconvConfig.excitationLowHz = request.sweep.startHz;
    deconvConfig.excitationHighHz = request.sweep.endHz;
    deconvConfig.trustedLowHz = fLo;
    deconvConfig.trustedHighHz = fHi;

    // The loop, raw: its peak is t = 0, and its in-band mean is 0 dB.
    const auto loop = rta::ir::deconvolve(capture.reference, inverse, deconvConfig);
    const Peak refPeak = findPeak(loop.samples);
    // Positive peak only. The analysis pulse is positive by construction
    // (every term of the lag-(Ns-1) sum is non-negative, Deconvolver.cpp), so
    // a negative maximum means the loop is inverted or is not a loop, and
    // re-referencing to it would put t = 0 on a sidelobe.
    if (!(refPeak.value > 0.0f) || refPeak.peakToRms < kMinPeakToRms ||
        refPeak.index < loop.originIndex) {
        return refuse(SweepIrStatus::ReferenceNotFound,
                      "no reference peak found -- is REF wired to the output?");
    }

    // How long the capture ran after the sweep left the output, counted from
    // the loop's own arrival of the sweep start.
    const std::size_t referenceArrival = refPeak.index - loop.originIndex;
    const std::size_t sweepEnd = referenceArrival + sweepSamples;
    const std::size_t tailSamples =
        capture.reference.size() > sweepEnd ? capture.reference.size() - sweepEnd : 0;
    if (tailSamples < requiredTailSamples(request)) {
        return refuse(SweepIrStatus::TailTooShort, "capture ended before the required decay time");
    }

    deconvConfig.normalisationGain = rta::ir::inBandNormalisation(loop, fLo, fHi);
    const auto loopFlatness = rta::ir::bandFlatness(loop, fLo, fHi);
    auto measurement = rta::ir::deconvolve(capture.measurement, inverse, deconvConfig);
    const Peak measPeak = findPeak(measurement.samples);
    if (!(measPeak.value != 0.0f)) {
        return refuse(SweepIrStatus::MeasurementNotFound, "the measurement channel holds no signal");
    }

    const long delay = static_cast<long>(measPeak.index) - static_cast<long>(refPeak.index);
    const std::size_t leadIn = sweepIrLeadInSamples(fs, fLo);
    const std::size_t fftSize = sweepIrWindowSize(fs, fLo);
    const auto minIrAfterArrival = static_cast<long>(std::ceil(kMinIrAfterArrivalSeconds * fs));
    if (delay > static_cast<long>(fftSize) - static_cast<long>(leadIn) - minIrAfterArrival ||
        delay < -static_cast<long>(leadIn) || refPeak.index < leadIn) {
        return refuse(SweepIrStatus::ArrivalOutOfWindow,
                      "the arrival does not fit the analysis window");
    }

    // The N-point window, rectangular, from `leadIn` samples before the loop
    // peak. Samples past the end of the deconvolution are zeros: the tail check
    // above normally rules that out, but a caller may ask for a short tail.
    // Handing analyseSpectrum a Deconvolution whose origin sits `leadIn`
    // samples in makes IT start at index 0 and run exactly N samples, so the
    // lead-in's linear phase is removed by the one routine that already does
    // that (record sec. 7) rather than re-derived here.
    rta::ir::Deconvolution window;
    window.samples.assign(fftSize, 0.0f);
    const std::size_t start = refPeak.index - leadIn;
    const std::size_t available =
        measurement.samples.size() > start ? measurement.samples.size() - start : 0;
    std::copy_n(measurement.samples.begin() + static_cast<std::ptrdiff_t>(start),
                std::min(fftSize, available), window.samples.begin());
    window.originIndex = leadIn;
    window.sampleRate = fs;
    const auto spectrum = rta::ir::analyseSpectrum(window, rta::ir::IrSpectrumConfig{fLo});

    SweepIrOutcome out;
    out.status = SweepIrStatus::Ok;
    out.ir.originIndex = refPeak.index;  // metadata only; the samples are untouched
    out.ir.arrivalSamples = delay;
    out.ir.tailSamples = tailSamples;
    out.ir.sampleRate = fs;
    out.ir.normalisationGain = measurement.normalisationGain;
    out.ir.harmonicSpacingL = measurement.harmonicSpacingL;
    out.ir.excitationLowHz = measurement.excitationLowHz;
    out.ir.excitationHighHz = measurement.excitationHighHz;
    out.ir.trustedLowHz = measurement.trustedLowHz;
    out.ir.trustedHighHz = measurement.trustedHighHz;
    out.ir.samples = std::move(measurement.samples);
    out.referenceArrivalSamples = referenceArrival;
    out.delaySamples = delay;
    out.loopMinDb = loopFlatness.minDb;
    out.loopMaxDb = loopFlatness.maxDb;
    out.measurementClipped = std::any_of(capture.measurement.begin(), capture.measurement.end(),
                                         [](float x) { return std::fabs(x) >= 1.0f; });
    out.sampleRate = fs;
    out.fftSize = fftSize;
    out.binHz = spectrum.binHz;
    out.bins = spectrum.bins;
    return out;
}

}  // namespace

std::size_t sweepIrLeadInSamples(double sampleRate, double lowBandEdgeHz) {
    return rta::ir::leadInSamplesFor(sampleRate, lowBandEdgeHz);
}

std::size_t sweepIrWindowSize(double sampleRate, double lowBandEdgeHz) {
    const auto halfSecond = static_cast<std::size_t>(std::ceil(0.5 * sampleRate));
    return nextPowerOfTwo(halfSecond + sweepIrLeadInSamples(sampleRate, lowBandEdgeHz));
}

SweepIrOutcome analyseSweepCapture(const SweepCapture& capture, const SweepIrRequest& request) {
    try {
        return analyseChecked(capture, request);
    } catch (const std::exception& e) {
        return refuse(SweepIrStatus::Failed, e.what());
    }
}

}  // namespace rta::measure
