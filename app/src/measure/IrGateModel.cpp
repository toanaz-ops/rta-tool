// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. See IrGateModel.h.
#include "measure/IrGateModel.h"

#include "rta/dsp/ExcessPhase.h"
#include "rta/ir/IrGate.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>

namespace rta::measure {
namespace {

// The peak-hold window of the ETC envelope: 0.25 ms (see IrGateModel.h).
constexpr double kEnvelopeWindowMs = 0.25;
// Floor of the drawn envelope, so an exactly-silent IR is -120 dB, not -inf.
constexpr float kEtcFloorDb = -120.0f;

std::string oneDecimal(double value) {
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "%.1f", value);
    return buffer;
}

std::string markersText(const IrGateMarkers& m) { return oneDecimal(m.startMs) + ".." + oneDecimal(m.endMs) + " ms"; }

}  // namespace

std::string validAboveText(double fMinHz) {
    return "valid above " + std::to_string(static_cast<long>(std::ceil(fMinHz - 1.0e-9))) + " Hz";
}

IrEtc buildEtc(const SweepIr& ir) {
    IrEtc etc;
    if (ir.samples.empty() || !(ir.sampleRate > 0.0)) return etc;
    const double msPerSample = 1000.0 / ir.sampleRate;
    const long long arrival = static_cast<long long>(ir.originIndex) + ir.arrivalSamples;
    const long long size = static_cast<long long>(ir.samples.size());
    const long long first = std::max<long long>(0, arrival - std::llround(kEtcLeadMs / msPerSample));
    const long long last = std::min<long long>(size - 1, arrival + std::llround(kEtcSpanMs / msPerSample));
    if (arrival < 0 || arrival >= size || last <= first) return etc;

    const long long half = std::max<long long>(1, std::llround(0.5 * kEnvelopeWindowMs / msPerSample));
    std::vector<float> peak;
    peak.reserve(static_cast<std::size_t>(last - first + 1));
    float top = 0.0f;
    for (long long i = first; i <= last; ++i) {
        float m = 0.0f;
        const long long lo = std::max<long long>(0, i - half);
        const long long hi = std::min<long long>(size - 1, i + half);
        for (long long j = lo; j <= hi; ++j) m = std::max(m, std::fabs(ir.samples[static_cast<std::size_t>(j)]));
        peak.push_back(m);
        top = std::max(top, m);
    }
    etc.startMs = static_cast<double>(first - arrival) * msPerSample;
    etc.stepMs = msPerSample;
    etc.db.resize(peak.size());
    for (std::size_t i = 0; i < peak.size(); ++i) {
        etc.db[i] = top > 0.0f ? std::max(kEtcFloorDb, 20.0f * std::log10(std::max(peak[i], 1.0e-12f) / top))
                               : kEtcFloorDb;
    }
    return etc;
}

IrGateAnalysis analyseIrGate(const SweepIr& ir, const IrGateMarkers& markers) {
    IrGateAnalysis out;
    out.markers = markers;
    out.sampleRate = ir.sampleRate;
    rta::ir::GateSpec spec;
    spec.startMs = markers.startMs;
    spec.endMs = markers.endMs;
    spec.arrivalSamples = static_cast<long>(ir.arrivalSamples);
    const auto result = rta::ir::gateSpectrum(ir.samples, ir.originIndex, ir.sampleRate, spec);
    if (!result.ok()) {
        out.refusal = result.message;
        out.readout = "GATE refused -- " + result.message;
        return out;
    }
    out.ok = true;
    out.fMinHz = result.fMinHz;
    out.bins = result.spectrum.bins;
    out.transformSize = (out.bins.size() - 1) * 2;
    out.readout = "GATE " + markersText(markers) + " \xc2\xb7 " + validAboveText(result.fMinHz);
    out.windowText = "gate " + markersText(markers) + ", " + validAboveText(result.fMinHz);
    return out;
}

std::string IrExcessPhase::tau0Text() const {
    if (!valid) return {};
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "tau0 %.2f ms", broadbandDelayMs);
    return buffer;
}

namespace {

constexpr double kRadToDeg = 180.0 / std::numbers::pi;

// Display unwrap, in double: add or subtract 2*pi where a step exceeds pi. Not
// PhaseUnwrap.cpp's float version -- this runs on 8193 bins once per settle and
// a float running offset would add rounding the picture does not need.
std::vector<float> unwrappedDegrees(const std::vector<double>& wrappedRad) {
    std::vector<float> out(wrappedRad.size());
    double offset = 0.0;
    double previous = wrappedRad.empty() ? 0.0 : wrappedRad.front();
    for (std::size_t k = 0; k < wrappedRad.size(); ++k) {
        const double step = wrappedRad[k] - previous;
        if (step > std::numbers::pi) offset -= 2.0 * std::numbers::pi;
        else if (step < -std::numbers::pi) offset += 2.0 * std::numbers::pi;
        previous = wrappedRad[k];
        out[k] = static_cast<float>((wrappedRad[k] + offset) * kRadToDeg);
    }
    return out;
}

bool isPowerOfTwoPlusOne(std::size_t n) { return n >= 5 && ((n - 1) & (n - 2)) == 0; }

}  // namespace

IrExcessPhase excessPhaseFor(std::span<const std::complex<float>> bins, double sampleRate, double fMinHz,
                             double trustedHighHz) {
    IrExcessPhase out;
    if (!isPowerOfTwoPlusOne(bins.size()) || !(sampleRate > 0.0)) {
        out.refusal = "excess phase needs a 2^k + 1 bin spectrum and a positive sample rate";
        return out;
    }
    out.refusal = "too few trusted bins for excess phase";
    const std::size_t n = bins.size();
    out.binHz = sampleRate / (2.0 * static_cast<double>(n - 1));

    std::vector<float> magnitude(n);
    std::vector<std::complex<double>> h(n);
    std::vector<std::uint8_t> trusted(n, 0);
    for (std::size_t k = 0; k < n; ++k) {
        h[k] = {static_cast<double>(bins[k].real()), static_cast<double>(bins[k].imag())};
        magnitude[k] = std::abs(bins[k]);
        const double hz = static_cast<double>(k) * out.binHz;
        trusted[k] = (hz >= fMinHz && hz <= trustedHighHz) ? 1 : 0;
    }
    const auto kernel = rta::dsp::excessPhase(magnitude, h, trusted, sampleRate);
    if (!kernel.valid) return out;

    out.valid = true;
    out.refusal.clear();
    out.trustedLowHz = fMinHz;
    out.trustedHighHz = trustedHighHz;
    out.broadbandDelayMs = kernel.broadbandDelaySec * 1000.0;

    // arg(H_min) = arg(H) + w*tau0 - phi_x: the kernel's own definition,
    // phi_x = arg((H/H_min) * e^{+jw tau0}), solved for H_min's phase.
    std::vector<double> measured(n), minimum(n);
    out.excessDeg.resize(n);
    out.excessGroupDelayMs.resize(n);
    for (std::size_t k = 0; k < n; ++k) {
        const double w = 2.0 * std::numbers::pi * static_cast<double>(k) * out.binHz;
        measured[k] = std::arg(h[k]);
        // One complex product then ONE arg(): the sum of two wrapped angles and a ramp can sit
        // several turns off, and the unwrap below only repairs steps of one.
        minimum[k] = std::arg(h[k] * std::polar(1.0, w * kernel.broadbandDelaySec - static_cast<double>(kernel.excessPhaseRad[k])));
        out.excessDeg[k] = static_cast<float>(static_cast<double>(kernel.excessPhaseRad[k]) * kRadToDeg);
        out.excessGroupDelayMs[k] = static_cast<float>((kernel.excessGroupDelay[k] - kernel.broadbandDelaySec) * 1000.0);
    }
    out.measuredDeg = unwrappedDegrees(measured);
    out.minimumDeg = unwrappedDegrees(minimum);
    return out;
}

std::optional<rta::trace::Trace> gatedTrace(const SweepIr& ir, const IrGateAnalysis& analysis,
                                            std::string captureId, std::string deviceName,
                                            std::string channelRoles) {
    if (!analysis.ok || analysis.bins.empty()) return std::nullopt;
    rta::trace::CaptureMeta meta;
    meta.id = std::move(captureId);
    meta.capturedAtUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
    meta.deviceName = std::move(deviceName);
    meta.channelRoles = std::move(channelRoles);
    meta.sampleRate = analysis.sampleRate;
    meta.fftSize = static_cast<int>(analysis.transformSize);
    meta.window = analysis.windowText;
    meta.averagingType = "None";
    meta.averagingDepth = 0;
    meta.effectiveAverages = 1.0;  // one sweep, nothing averaged
    meta.appliedDelaySamples = static_cast<int>(ir.arrivalSamples);

    std::vector<float> magnitudeDb(analysis.bins.size());
    std::vector<float> phase(analysis.bins.size());
    for (std::size_t k = 0; k < analysis.bins.size(); ++k) {
        magnitudeDb[k] = 20.0f * std::log10(std::max(std::abs(analysis.bins[k]), 1.0e-10f));
        phase[k] = std::arg(analysis.bins[k]);
    }
    auto trace = rta::trace::Trace::make(std::move(meta), std::move(magnitudeDb));
    if (!trace.has_value()) return std::nullopt;
    if (!trace->setPhase(std::move(phase))) return std::nullopt;
    return trace;
}

}  // namespace rta::measure
