// SPDX-License-Identifier: AGPL-3.0-or-later
#include "trace/VirtualTrace.h"

#include "rta/dsp/Biquad.h"
#include "rta/eq/BiquadDesign.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rta::trace {
namespace {

/// The forward half of the conversion: dB + wrapped radians -> complex.
///
/// A bin sitting on TransferSnapshot::kMagnitudeFloorDb becomes 1e-6, which
/// is harmless in a sum -- against a unit bin it moves the result by
/// 20log10(1 + 1e-6) = 8.69e-6 dB, a figure test_virtual_trace.cpp asserts
/// rather than repeats (record Sec.5).
std::complex<double> toComplex(float magnitudeDb, float phaseRadians) noexcept {
    const double magnitude = std::pow(10.0, static_cast<double>(magnitudeDb) / 20.0);
    return std::polar(magnitude, static_cast<double>(phaseRadians));
}

/// The reverse half. Floored the way makeSnapshot() floors, so a preview and a
/// measurement of the same response read the same at the bottom of the scale
/// instead of one of them running off to -inf.
float toMagnitudeDb(const std::complex<double>& h) noexcept {
    const double magnitude = std::abs(h);
    if (magnitude <= 0.0) return rta::dsp::TransferSnapshot::kMagnitudeFloorDb;
    const double db = 20.0 * std::log10(magnitude);
    return static_cast<float>(
        std::max(db, static_cast<double>(rta::dsp::TransferSnapshot::kMagnitudeFloorDb)));
}

/// One chain step, in place through a scratch buffer. Every op is an
/// rta::dsp:: call: this file owns the CONVERSION, never the arithmetic.
void applyOp(const VirtualOp& op, double binWidthHz, std::vector<std::complex<double>>& buffer,
             std::vector<std::complex<double>>& scratch) {
    scratch.assign(buffer.size(), {});

    if (const auto* delay = std::get_if<DelayOp>(&op)) {
        rta::dsp::applyDelay(buffer, binWidthHz, delay->tauSeconds, scratch);
    } else if (std::get_if<PolarityOp>(&op) != nullptr) {
        rta::dsp::applyPolarity(buffer, scratch);
    } else if (const auto* gain = std::get_if<GainOp>(&op)) {
        rta::dsp::applyGain(buffer, gain->gainLinear, scratch);
    } else if (const auto* biquads = std::get_if<BiquadOp>(&op)) {
        std::vector<rta::dsp::Biquad::Coeffs> sections;
        sections.reserve(biquads->specs.size());
        for (const auto& spec : biquads->specs) {
            // Skipped, not thrown: this runs under a slider. designBiquad
            // refuses fc at or past Nyquist and a non-positive Q by throwing,
            // which is right for a solver and wrong for a preview frame.
            try {
                sections.push_back(rta::eq::designBiquad(spec, biquads->sampleRate));
            } catch (const std::invalid_argument&) {
                continue;
            }
        }
        rta::dsp::applyBiquads(buffer, binWidthHz, biquads->sampleRate, sections, scratch);
    }

    buffer.swap(scratch);
}

}  // namespace

std::optional<VirtualTrace> VirtualTrace::fromTrace(const Trace& trace) {
    if (!trace.has(Field::Magnitude) || !trace.has(Field::Phase)) return std::nullopt;
    const double binWidthHz = trace.binHz();
    if (!(binWidthHz > 0.0)) return std::nullopt;

    const auto magnitudeDb = trace.field(Field::Magnitude);
    const auto phase = trace.field(Field::Phase);
    if (magnitudeDb.size() != phase.size()) return std::nullopt;

    VirtualTrace result;
    result.binWidthHz_ = binWidthHz;
    result.source_.resize(magnitudeDb.size());
    for (std::size_t k = 0; k < magnitudeDb.size(); ++k) {
        result.source_[k] = toComplex(magnitudeDb[k], phase[k]);
    }

    const auto coherence = trace.field(Field::Coherence);
    if (coherence.size() == magnitudeDb.size()) {
        result.coherence_.assign(coherence.begin(), coherence.end());
    }
    return result;
}

std::optional<VirtualTrace> VirtualTrace::fromSnapshot(const rta::dsp::TransferSnapshot& snapshot) {
    if (snapshot.h.empty() || !(snapshot.binWidthHz > 0.0)) return std::nullopt;

    VirtualTrace result;
    result.binWidthHz_ = snapshot.binWidthHz;
    result.source_ = snapshot.h;
    if (snapshot.coherence.has_value() && snapshot.coherence->size() == snapshot.h.size()) {
        result.coherence_ = *snapshot.coherence;
    }
    return result;
}

VirtualRender VirtualTrace::render() const {
    std::vector<std::complex<double>> buffer = source_;
    std::vector<std::complex<double>> scratch;
    for (const auto& op : chain_) applyOp(op, binWidthHz_, buffer, scratch);

    VirtualRender rendered;
    rendered.magnitudeDb.resize(buffer.size());
    rendered.phaseRadians.resize(buffer.size());
    for (std::size_t k = 0; k < buffer.size(); ++k) {
        rendered.magnitudeDb[k] = toMagnitudeDb(buffer[k]);
        rendered.phaseRadians[k] = static_cast<float>(std::arg(buffer[k]));
    }
    // Copied, not recomputed: the chain has no opinion about trust.
    rendered.coherence = coherence_;
    rendered.h = std::move(buffer);
    return rendered;
}

VirtualSum sumOf(const VirtualTrace& a, const VirtualTrace& b) {
    const VirtualRender renderA = a.render();
    const VirtualRender renderB = b.render();

    std::optional<std::vector<float>> coherenceA;
    std::optional<std::vector<float>> coherenceB;
    if (!renderA.coherence.empty()) coherenceA = renderA.coherence;
    if (!renderB.coherence.empty()) coherenceB = renderB.coherence;

    rta::dsp::SummedResponse core =
        rta::dsp::sumResponses(renderA.h, renderB.h, coherenceA, coherenceB);

    VirtualSum summed;
    summed.magnitudeDb.resize(core.h.size());
    summed.phaseRadians.resize(core.h.size());
    for (std::size_t k = 0; k < core.h.size(); ++k) {
        summed.magnitudeDb[k] = toMagnitudeDb(core.h[k]);
        summed.phaseRadians[k] = static_cast<float>(std::arg(core.h[k]));
    }
    summed.summationTrust = std::move(core.summationTrust);
    summed.trustPresent = core.trustPresent;
    summed.h = std::move(core.h);
    return summed;
}

}  // namespace rta::trace
