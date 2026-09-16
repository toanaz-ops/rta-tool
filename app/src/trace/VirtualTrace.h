// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE, no Qt, no audio-device API.
//
// L7-ALIGN task G (docs/plans/2026-09-15-L7-align-impl-plan.md; decision
// record docs/dsp/2026-09-06-l7-alignment-wizard.md Sec.5, Sec.10.10).
//
// THE ONE dB<->COMPLEX CONVERSION POINT. A stored Trace is float dB plus
// wrapped radians; the live TransferSnapshot carries complex h; the five G11
// ops in rta::dsp take complex spans. Somebody has to convert, and the rule
// L4a decision 7 applies to degrees applies here to decibels: convert at
// exactly one place, or two places drift and no test can say which one is
// right.
//
// WHAT THIS TYPE IS NOT, AND WHY THAT IS THE POINT. A VirtualTrace is a
// PREVIEW: what a delay, a polarity flip, a gain trim or a filter bank WOULD
// do to a measurement. It is not a measurement. So it carries no CaptureMeta
// -- there is no device, no time, no window, no averaging behind it to
// describe -- it is not a Trace, and it cannot reach TraceLibrary, because
// TraceLibrary::add takes a Trace BY VALUE (TraceLibrary.h:63). The barrier
// is the type system and not a naming convention (record Sec.5, research D8):
// a prediction saved to the library would come back next week indistinguish-
// able from something a microphone heard.
#pragma once

#include "trace/Trace.h"

#include "rta/dsp/TransferEstimator.h"
#include "rta/dsp/VirtualProcessor.h"
#include "rta/eq/FilterSpec.h"

#include <complex>
#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

namespace rta::trace {

/// Positive tau means this source arrives LATER -- the sign
/// DelayEstimate::delaySamples already carries (memory/dual-fft-conventions.md
/// item 2), and the one rta::dsp::applyDelay implements.
struct DelayOp {
    double tauSeconds = 0.0;
};

struct PolarityOp {};

/// LINEAR, not dB. The op is a multiply; asking a caller which of the two a
/// field means is how the two spellings of one number start to drift.
struct GainOp {
    double gainLinear = 1.0;
};

/// ALIGN-R9: the chain consumes rta::eq::FilterSpec -- the vocabulary L7-EQ
/// already places and L7-FIR already exports -- and designs the coefficients
/// itself through rta::eq::designBiquad. It needs nothing from EQ's session
/// model, so this lane never waited on EQ task E/F; when that vector exists,
/// it IS the input, with no code change here.
///
/// A spec designBiquad refuses (fc at or past Nyquist, non-positive Q) is
/// SKIPPED rather than throwing: this runs on a preview path a slider drags
/// through, and a throw there takes out the frame, not the bad filter.
struct BiquadOp {
    std::vector<rta::eq::FilterSpec> specs;
    double sampleRate = 48000.0;
};

using VirtualOp = std::variant<DelayOp, PolarityOp, GainOp, BiquadOp>;

/// One source, chain applied, converted back for display.
struct VirtualRender {
    std::vector<std::complex<double>> h;
    std::vector<float> magnitudeDb;   ///< 20log10|H|, floored at kMagnitudeFloorDb
    std::vector<float> phaseRadians;  ///< wrapped to (-pi, pi] by std::arg

    /// The SOURCE's trust, passed through unchanged. Delay, polarity and gain
    /// do not change how much a measurement is trusted, and neither does
    /// previewing a filter over it -- the evidence is the same evidence
    /// (record Sec.5). EMPTY when the source had none: a zero-filled
    /// placeholder reads as "measured, and totally untrusted", which is a
    /// different claim from "not measured"
    /// (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
    std::vector<float> coherence;
};

/// Two sources summed.
///
/// A SEPARATE type from VirtualRender, which is the whole reason this struct
/// exists: the sum is not an estimate any cross-spectrum defines (record
/// Sec.5; L6b Sec.4 makes the same point for the spatial average), so it has
/// no coherence to carry, and a field named `coherence` here would be a
/// second writer beside TransferEstimator.cpp's makeSnapshot(). Keeping the
/// two returns apart means the sum has nowhere to put one even by accident.
struct VirtualSum {
    std::vector<std::complex<double>> h;
    std::vector<float> magnitudeDb;
    std::vector<float> phaseRadians;

    /// min(gamma^2_A, gamma^2_B) per bin, straight from
    /// rta::dsp::SummedResponse::summationTrust. Empty when either source had
    /// no trust to contribute.
    std::vector<float> summationTrust;
    bool trustPresent = false;
};

class VirtualTrace {
public:
    /// Needs BOTH magnitude and phase: a single-channel capture has no phase
    /// (Trace.h's class comment), and inventing one is exactly what that type
    /// was shaped to prevent. Returns nullopt rather than a flat-phase
    /// stand-in.
    [[nodiscard]] static std::optional<VirtualTrace> fromTrace(const Trace& trace);

    /// The live path: h is already complex, so nothing is converted on the way
    /// IN. Refuses an empty snapshot or a non-positive binWidthHz.
    [[nodiscard]] static std::optional<VirtualTrace>
    fromSnapshot(const rta::dsp::TransferSnapshot& snapshot);

    void setChain(std::vector<VirtualOp> chain) { chain_ = std::move(chain); }
    [[nodiscard]] const std::vector<VirtualOp>& chain() const noexcept { return chain_; }

    [[nodiscard]] VirtualRender render() const;

    [[nodiscard]] double binWidthHz() const noexcept { return binWidthHz_; }
    [[nodiscard]] std::size_t pointCount() const noexcept { return source_.size(); }

private:
    VirtualTrace() = default;

    std::vector<std::complex<double>> source_;
    std::vector<float> coherence_;  ///< empty == absent, as above
    double binWidthHz_ = 0.0;
    std::vector<VirtualOp> chain_;
};

/// H_Sigma = H_A + H_B, each with its own chain already applied. Delegates the
/// arithmetic AND the trust rule to rta::dsp::sumResponses -- this function
/// converts, it does not re-derive.
[[nodiscard]] VirtualSum sumOf(const VirtualTrace& a, const VirtualTrace& b);

}  // namespace rta::trace
