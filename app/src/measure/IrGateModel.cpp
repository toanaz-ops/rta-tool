// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. See IrGateModel.h.
#include "measure/IrGateModel.h"

#include "rta/ir/IrGate.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

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
