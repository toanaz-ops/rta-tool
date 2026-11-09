// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/ir/IrGate.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace rta::ir {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::string oneDecimal(double value) {
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "%.1f", value);
    return buffer;
}

std::size_t nextPowerOfTwo(std::size_t n) {
    std::size_t size = 4;
    while (size < n) size <<= 1;
    return size;
}

GateResult refuse(GateRefusal why, double value, std::string message) {
    GateResult out;
    out.refusal = why;
    out.refusalValue = value;
    out.message = std::move(message);
    return out;
}

// See the formulas on GateSpec.
double weight(std::size_t n, std::size_t length, std::size_t fadeIn, std::size_t fadeOut) {
    if (n < fadeIn) return 0.5 * (1.0 - std::cos(kPi * static_cast<double>(n) / static_cast<double>(fadeIn)));
    const std::size_t fadeOutStart = length - fadeOut;
    if (n >= fadeOutStart) {
        return 0.5 * (1.0 + std::cos(kPi * static_cast<double>(n - fadeOutStart) / static_cast<double>(fadeOut)));
    }
    return 1.0;
}

}  // namespace

GateResult gateSpectrum(std::span<const float> samples, std::size_t originIndex, double sampleRate,
                        const GateSpec& spec) {
    if (samples.empty()) return refuse(GateRefusal::EmptyIr, 0.0, "the impulse response is empty");
    if (!(sampleRate > 0.0)) {
        return refuse(GateRefusal::BadSampleRate, sampleRate, "the sample rate is not positive");
    }
    const double msPerSample = 1000.0 / sampleRate;
    const long startSamples = std::lround(spec.startMs / msPerSample);
    const long endSamples = std::lround(spec.endMs / msPerSample);
    const long length = endSamples - startSamples;
    if (!(spec.endMs > spec.startMs)) {
        return refuse(GateRefusal::StartNotBeforeEnd, spec.endMs - spec.startMs,
                      "gate start " + oneDecimal(spec.startMs) + " ms is not before its end " +
                          oneDecimal(spec.endMs) + " ms");
    }
    if (length <= 0) {
        // endMs > startMs, but both round to the same sample: a different mistake
        // from an inverted gate, and the sentence must not call it one.
        return refuse(GateRefusal::GateRoundsToNothing, spec.endMs - spec.startMs,
                      "gate " + oneDecimal(spec.startMs) + ".." + oneDecimal(spec.endMs) +
                          " ms is shorter than one sample (" + oneDecimal(msPerSample) + " ms)");
    }
    const auto fadeIn = static_cast<std::size_t>(std::lround(std::max(0.0, spec.fadeInMs) / msPerSample));
    const auto fadeOut = static_cast<std::size_t>(
        std::lround(std::clamp(spec.fadeOutFraction, 0.0, 1.0) * static_cast<double>(length)));
    const auto gateLength = static_cast<std::size_t>(length);
    if (fadeIn + fadeOut > gateLength) {
        const double gateMs = static_cast<double>(length) * msPerSample;
        return refuse(GateRefusal::GateShorterThanFades, gateMs,
                      "gate " + oneDecimal(gateMs) + " ms is shorter than its fades (" +
                          oneDecimal(static_cast<double>(fadeIn) * msPerSample) + " ms in + " +
                          oneDecimal(static_cast<double>(fadeOut) * msPerSample) + " ms out)");
    }

    // The arrival is gate index -S. Full weight needs the opening ramp finished
    // there (w[n] = 1 for n >= Fi), so `-S < Fi` (any start later than -Fi samples,
    // including every start after the arrival) would attenuate or delete the direct sound; the latest start that keeps it is -Fi samples.
    if (-startSamples < static_cast<long>(fadeIn)) {
        return refuse(GateRefusal::DirectSoundInFadeIn, spec.startMs,
                      "gate start " + oneDecimal(spec.startMs) +
                          " ms would attenuate or remove the direct sound (latest start " +
                          oneDecimal(-static_cast<double>(fadeIn) * msPerSample) + " ms)");
    }

    // Signed throughout: the arrival may sit before index 0 of a hand-built IR.
    const long long arrivalIndex = static_cast<long long>(originIndex) + spec.arrivalSamples;
    const long long firstIndex = arrivalIndex + startSamples;
    const long long endIndex = arrivalIndex + endSamples;
    if (firstIndex < 0) {
        return refuse(GateRefusal::StartBeforeIr, spec.startMs,
                      "gate start " + oneDecimal(spec.startMs) +
                          " ms is before the first sample of the IR (earliest " +
                          oneDecimal(-static_cast<double>(arrivalIndex) * msPerSample) + " ms)");
    }
    if (endIndex > static_cast<long long>(samples.size())) {
        const double latestMs =
            static_cast<double>(static_cast<long long>(samples.size()) - arrivalIndex) * msPerSample;
        return refuse(GateRefusal::EndPastIr, spec.endMs,
                      "gate end " + oneDecimal(spec.endMs) + " ms is past the last sample of the IR (latest " +
                          oneDecimal(latestMs) + " ms)");
    }

    // The gated copy: index `pre` of `trimmed` is the arrival, so analyseSpectrum
    // sees originIndex == pre and its lead-in rotation references phase to the
    // direct sound. `pre` is at least 1 (a gate that starts at or after the
    // arrival has no pre-arrival samples, but a lead-in of 0 would mean a band
    // edge of infinity), and `2*fs/pre` as the band edge makes the routine's
    // own `round(2*fs/edge)` hand back exactly `pre`.
    const auto pre = static_cast<std::size_t>(std::max<long>(1, -startSamples));
    const auto firstSlot = static_cast<std::size_t>(static_cast<long long>(pre) + startSamples);
    const std::size_t needed = firstSlot + gateLength;
    const std::size_t size = std::max(spec.transformSize, nextPowerOfTwo(std::max(needed, pre + 1)));

    Deconvolution trimmed;
    trimmed.samples.assign(size, 0.0f);
    for (std::size_t n = 0; n < gateLength; ++n) {
        const double w = weight(n, gateLength, fadeIn, fadeOut);
        trimmed.samples[firstSlot + n] =
            static_cast<float>(w * static_cast<double>(samples[static_cast<std::size_t>(firstIndex) + n]));
    }
    trimmed.originIndex = pre;
    trimmed.sampleRate = sampleRate;

    GateResult out;
    out.spectrum = analyseSpectrum(trimmed, IrSpectrumConfig{2.0 * sampleRate / static_cast<double>(pre)});
    out.startSamples = startSamples;
    out.endSamples = endSamples;
    out.fadeInSamples = fadeIn;
    out.fadeOutSamples = fadeOut;
    out.fMinHz = sampleRate / (static_cast<double>(gateLength) - 0.5 * static_cast<double>(fadeOut));
    return out;
}

}  // namespace rta::ir
