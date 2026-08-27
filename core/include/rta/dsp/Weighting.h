// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/dsp/Biquad.h"

#include <span>
#include <string_view>

namespace rta::dsp {

/// IEC 61672-1 clause 4 frequency weightings, plus flat (Z, no filtering).
enum class WeightingType { A, C, Z };

[[nodiscard]] std::string_view toString(WeightingType type) noexcept;

/// A/C/Z frequency weighting, realised as a bilinear-transformed SOS cascade
/// of the IEC 61672-1 Annex E analog poles/zeros (design chain: see
/// docs/dsp/2026-08-27-weighting-and-meters.md and
/// docs/plans/2026-08-27-weighting-meters-impl-plan.md section 4).
///
/// This design's conformance to IEC 61672-1's full tolerance envelope is
/// NOT verified and must never be asserted anywhere in this file or its
/// tests -- that envelope is paywalled and only four points are corroborated
/// (see the decision record). The honest claim, and the only one this code
/// makes, is: analytic weighting within 0.05 dB of Table 3; digital filter
/// error as published in the decision record.
class Weighting {
public:
    /// @param sampleRate  hertz; must be > 0.
    /// @throws std::invalid_argument if sampleRate <= 0.
    Weighting(WeightingType type, double sampleRate);

    /// The ANALYTIC (continuous-time) weighting in dB at `frequencyHz`.
    ///
    /// This is the design target and the closed form the tests assert
    /// against, not a convenience: it is IEC 61672-1 Annex E evaluated
    /// directly, with no sample rate anywhere in it. The digital cascade's
    /// response is an APPROXIMATION of this function; the size of that
    /// approximation is published in
    /// docs/dsp/2026-08-27-weighting-and-meters.md.
    [[nodiscard]] static double analyticDb(double frequencyHz, WeightingType type) noexcept;

    [[nodiscard]] WeightingType type() const noexcept { return type_; }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }

    /// The DIGITAL cascade's magnitude response in dB at `frequencyHz`. Off
    /// the audio path; for tests, plots and the published error table.
    /// `20*log10|H(e^{jw})|`, computed from BiquadCascade::attenuationDb
    /// (which is -20*log10|H|, positive = down), so this is the negation.
    [[nodiscard]] double responseDb(double frequencyHz) const noexcept;

    [[nodiscard]] const BiquadCascade& cascade() const noexcept { return cascade_; }

    void reset() noexcept { cascade_.reset(); }

    /// Z (flat) holds zero sections, so BiquadCascade::process with an empty
    /// cascade already copies input to output bit-identically -- float ->
    /// double -> float round-trips losslessly with no arithmetic in between.
    /// No special case is needed here for Z.
    void process(std::span<const float> in, std::span<float> out) noexcept {
        cascade_.process(in, out);
    }

    [[nodiscard]] float processSample(float x) noexcept {
        return static_cast<float>(cascade_.processSample(static_cast<double>(x)));
    }

private:
    WeightingType type_;
    double        sampleRate_;
    BiquadCascade cascade_;  // empty for Z
};

}  // namespace rta::dsp
