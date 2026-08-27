// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace rta::dsp {

/// One second-order section, Direct Form II Transposed, double throughout.
///
/// DF2T is the form scipy's sosfilt uses and the form with the best rounding
/// behaviour for the pole radii this bank works at -- the lowest third-octave
/// band at 48 kHz sits 8.7e-5 from the unit circle, where a Direct Form I
/// accumulator loses the difference between a decaying and a growing mode.
struct Biquad {
    /// a0 is normalised to 1 and therefore not stored: storing a coefficient
    /// that is always one invites a caller to set it to something else.
    struct Coeffs { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };
    struct State  { double s1 = 0.0, s2 = 0.0; };

    ///     y  = b0*x + s1
    ///     s1 = b1*x - a1*y + s2
    ///     s2 = b2*x - a2*y
    static double processSample(const Coeffs& c, State& s, double x) noexcept {
        const double y = c.b0 * x + s.s1;
        s.s1 = c.b1 * x - c.a1 * y + s.s2;
        s.s2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

/// A run of sections applied in order. Owns its coefficients and its state.
class BiquadCascade {
public:
    BiquadCascade() = default;
    explicit BiquadCascade(std::vector<Biquad::Coeffs> sections)
        : sections_(std::move(sections)), state_(sections_.size()) {}

    [[nodiscard]] std::size_t size() const noexcept { return sections_.size(); }
    [[nodiscard]] std::span<const Biquad::Coeffs> sections() const noexcept { return sections_; }

    /// Allocation-free. Sections run ascending, which is also the order the
    /// gain was placed in, so the running value stays near unity magnitude
    /// even for a band whose overall gain is 1.9e-22.
    double processSample(double x) noexcept {
        double y = x;
        for (std::size_t i = 0; i < sections_.size(); ++i) {
            y = Biquad::processSample(sections_[i], state_[i], y);
        }
        return y;
    }

    void process(std::span<const float> in, std::span<float> out) noexcept {
        for (std::size_t n = 0; n < in.size(); ++n) {
            out[n] = static_cast<float>(processSample(static_cast<double>(in[n])));
        }
    }

    void reset() noexcept {
        for (auto& s : state_) s = Biquad::State{};
    }

    /// Largest |pole| over all sections. A cascade with any value >= 1 is not
    /// a filter, it is a sawtooth generator, so this is asserted in CI.
    [[nodiscard]] double maxPoleRadius() const noexcept {
        double worst = 0.0;
        for (const auto& c : sections_) worst = std::max(worst, sectionPoleRadius(c));
        return worst;
    }

    /// -20*log10|H(e^{jw})|, in decibels of ATTENUATION (positive = down).
    ///
    /// Summed per section rather than multiplied, because the product form
    /// spans 1e-22 to 1e+16 across a single low band's sections and the sum of
    /// logarithms cannot overflow at all.
    [[nodiscard]] static double attenuationDb(std::span<const Biquad::Coeffs> sections,
                                              double omegaRadiansPerSample) {
        double total = 0.0;
        for (const auto& c : sections) total += sectionAttenuationDb(c, omegaRadiansPerSample);
        return total;
    }

    [[nodiscard]] double attenuationDb(double omegaRadiansPerSample) const {
        return attenuationDb(sections_, omegaRadiansPerSample);
    }

private:
    /// Denominator polynomial is 1 + a1*z^-1 + a2*z^-2, i.e. z^2 + a1*z + a2 = 0
    /// in the z-plane. A genuine complex-conjugate pair has product a2 = |p|^2;
    /// a section that has degenerated to real poles is solved directly, since
    /// both are legal input (see W5/W9 in the design chain).
    [[nodiscard]] static double sectionPoleRadius(const Biquad::Coeffs& c) noexcept {
        const double disc = c.a1 * c.a1 - 4.0 * c.a2;
        if (disc < 0.0) {
            return std::sqrt(std::max(0.0, c.a2));
        }
        const double root = std::sqrt(disc);
        const double p1 = (-c.a1 + root) * 0.5;
        const double p2 = (-c.a1 - root) * 0.5;
        return std::max(std::abs(p1), std::abs(p2));
    }

    /// |H(e^{jw})| computed from real/imaginary parts directly -- avoids a
    /// <complex> dependency for a two-term numerator and denominator.
    [[nodiscard]] static double sectionAttenuationDb(const Biquad::Coeffs& c,
                                                      double w) noexcept {
        const double cw = std::cos(w), sw = std::sin(w);
        const double c2w = std::cos(2.0 * w), s2w = std::sin(2.0 * w);

        const double nRe = c.b0 + c.b1 * cw + c.b2 * c2w;
        const double nIm = -(c.b1 * sw + c.b2 * s2w);
        const double dRe = 1.0 + c.a1 * cw + c.a2 * c2w;
        const double dIm = -(c.a1 * sw + c.a2 * s2w);

        const double nMagSq = nRe * nRe + nIm * nIm;
        const double dMagSq = dRe * dRe + dIm * dIm;

        return 10.0 * std::log10(dMagSq) - 10.0 * std::log10(nMagSq);
    }

    std::vector<Biquad::Coeffs> sections_;
    std::vector<Biquad::State>  state_;
};

}  // namespace rta::dsp
