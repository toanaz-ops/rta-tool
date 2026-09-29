// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/EqTraceGrid.h"

#include "rta/dsp/FirDesign.h"

#include <algorithm>
#include <cmath>

namespace rta::measure {

std::vector<float> traceBinFrequencies(const rta::trace::Trace& trace) {
    std::vector<float> hz(trace.pointCount());
    const double binHz = trace.binHz();
    for (std::size_t k = 0; k < hz.size(); ++k) hz[k] = static_cast<float>(static_cast<double>(k) * binHz);
    return hz;
}

std::vector<std::complex<double>> traceComplexResponse(std::span<const float> magnitudeDb,
                                                       std::span<const float> phaseRad) {
    std::vector<std::complex<double>> h(magnitudeDb.size());
    for (std::size_t k = 0; k < h.size(); ++k) {
        h[k] = std::polar(std::pow(10.0, static_cast<double>(magnitudeDb[k]) / 20.0),
                          static_cast<double>(phaseRad[k]));
    }
    return h;
}

bool allFinite(std::span<const float> values) {
    return std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); });
}

std::vector<float> resampleTargetDb(std::span<const float> srcHz, std::span<const float> srcDb,
                                    std::span<const float> dstHz) {
    if (srcHz.empty()) return std::vector<float>(dstHz.size(), 0.0f);  // FLAT
    if (std::equal(srcHz.begin(), srcHz.end(), dstHz.begin(), dstHz.end())) {
        return std::vector<float>(srcDb.begin(), srcDb.end());
    }
    rta::dsp::FirTarget curve;
    for (std::size_t k = 0; k < srcHz.size(); ++k) {
        if (!(srcHz[k] > 0.0f)) continue;
        curve.frequencyHz.push_back(static_cast<double>(srcHz[k]));
        curve.gainDb.push_back(static_cast<double>(srcDb[k]));
    }
    std::vector<float> out(dstHz.size(), 0.0f);
    if (curve.frequencyHz.empty()) return out;
    for (std::size_t k = 0; k < out.size(); ++k) {
        out[k] = static_cast<float>(rta::dsp::interpolateFirTargetDb(curve, static_cast<double>(dstHz[k])));
    }
    return out;
}

}  // namespace rta::measure
