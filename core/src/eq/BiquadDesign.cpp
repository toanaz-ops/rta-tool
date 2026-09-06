// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/eq/BiquadDesign.h"

#include <cmath>
#include <numbers>
#include <span>
#include <stdexcept>

namespace rta::eq {

namespace {

constexpr double kPi = std::numbers::pi;

void validate(const FilterSpec& spec, double sampleRate) {
    if (sampleRate <= 0.0) {
        throw std::invalid_argument("designBiquad: sampleRate must be positive");
    }
    if (spec.fcHz <= 0.0) {
        throw std::invalid_argument("designBiquad: fcHz must be positive");
    }
    if (spec.fcHz >= sampleRate / 2.0) {
        throw std::invalid_argument("designBiquad: fcHz must be below Nyquist");
    }
    if (spec.q <= 0.0) {
        throw std::invalid_argument("designBiquad: q must be positive");
    }
    // Shelves only: the Q-parameterised alpha (shelfAlpha below, RBJ cookbook
    // Sec.6) is sqrt((A + 1/A)*(1/Q - 1) + 2), real only while that radicand
    // stays non-negative. High Q with a large |gainDb| drives it negative
    // (e.g. HighShelf/LowShelf at Q=8, gainDb=-15: (A+1/A)=2.793,
    // (1/Q-1)=-0.875, product -2.444, +2 = -0.444), which sqrt() turns into
    // NaN that then poisons every coefficient below -- this is the domain
    // boundary of the Q-parameterised form itself, not a numerical accident,
    // so it is refused here at the door rather than left to surface as NaN.
    // Peaking's alpha never depends on gain (designPeaking above), so it has
    // no such term and is not checked.
    if (spec.type == FilterType::LowShelf || spec.type == FilterType::HighShelf) {
        const double A = std::pow(10.0, spec.gainDb / 40.0);
        const double radicand = (A + 1.0 / A) * (1.0 / spec.q - 1.0) + 2.0;
        if (radicand <= 0.0) {
            throw std::invalid_argument(
                "designBiquad: shelf Q/gainDb combination is out of the "
                "Q-parameterised alpha's domain (radicand <= 0)");
        }
    }
}

/// a0 is normalised away here, once, so every design function below can write
/// the cookbook's own {b0,b1,b2,a0,a1,a2} without remembering to divide --
/// Biquad::Coeffs has no field for a0 (Biquad.h:20-21).
rta::dsp::Biquad::Coeffs normalize(double b0, double b1, double b2,
                                    double a0, double a1, double a2) noexcept {
    return rta::dsp::Biquad::Coeffs{ b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

/// RBJ cookbook peaking EQ, Q form: alpha does not depend on gain, which is
/// what makes a gain-inverted twin's {b} and {a} sets exactly swap (Task
/// W0-3 test T4) -- not a coincidence, the reason the cookbook is shaped
/// this way (a peaking boost and its own -G cut are reciprocal filters).
rta::dsp::Biquad::Coeffs designPeaking(double w0, double q, double gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double cw = std::cos(w0);

    const double b0 = 1.0 + alpha * A;
    const double b1 = -2.0 * cw;
    const double b2 = 1.0 - alpha * A;
    const double a0 = 1.0 + alpha / A;
    const double a1 = -2.0 * cw;
    const double a2 = 1.0 - alpha / A;
    return normalize(b0, b1, b2, a0, a1, a2);
}

/// Shared by both shelves: the Q-parameterised alpha (RBJ cookbook's
/// alternative to the S/slope form), sqrt(A) appearing because the shelf's
/// half-gain point is |H(jw0)| = A, not A^2 like peaking's (T5's asymmetry).
double shelfAlpha(double w0, double q, double A) noexcept {
    return std::sin(w0) / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / q - 1.0) + 2.0);
}

rta::dsp::Biquad::Coeffs designLowShelf(double w0, double q, double gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double cw = std::cos(w0);
    const double sqrtA = std::sqrt(A);
    const double alpha = shelfAlpha(w0, q, A);

    const double b0 = A * ((A + 1.0) - (A - 1.0) * cw + 2.0 * sqrtA * alpha);
    const double b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw);
    const double b2 = A * ((A + 1.0) - (A - 1.0) * cw - 2.0 * sqrtA * alpha);
    const double a0 = (A + 1.0) + (A - 1.0) * cw + 2.0 * sqrtA * alpha;
    const double a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw);
    const double a2 = (A + 1.0) + (A - 1.0) * cw - 2.0 * sqrtA * alpha;
    return normalize(b0, b1, b2, a0, a1, a2);
}

rta::dsp::Biquad::Coeffs designHighShelf(double w0, double q, double gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double cw = std::cos(w0);
    const double sqrtA = std::sqrt(A);
    const double alpha = shelfAlpha(w0, q, A);

    const double b0 = A * ((A + 1.0) + (A - 1.0) * cw + 2.0 * sqrtA * alpha);
    const double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
    const double b2 = A * ((A + 1.0) + (A - 1.0) * cw - 2.0 * sqrtA * alpha);
    const double a0 = (A + 1.0) - (A - 1.0) * cw + 2.0 * sqrtA * alpha;
    const double a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw);
    const double a2 = (A + 1.0) - (A - 1.0) * cw - 2.0 * sqrtA * alpha;
    return normalize(b0, b1, b2, a0, a1, a2);
}

}  // namespace

rta::dsp::Biquad::Coeffs designBiquad(const FilterSpec& spec, double sampleRate) {
    validate(spec, sampleRate);
    const double w0 = 2.0 * kPi * spec.fcHz / sampleRate;
    switch (spec.type) {
        case FilterType::Peaking:
            return designPeaking(w0, spec.q, spec.gainDb);
        case FilterType::LowShelf:
            return designLowShelf(w0, spec.q, spec.gainDb);
        case FilterType::HighShelf:
            return designHighShelf(w0, spec.q, spec.gainDb);
    }
    throw std::invalid_argument("designBiquad: unknown FilterType");
}

double responseDb(const FilterSpec& spec, double sampleRate, double hz) {
    const auto c = designBiquad(spec, sampleRate);
    const double w = 2.0 * kPi * hz / sampleRate;
    // -attenuationDb, not a re-derived |H| -- W0-R3's sign, reused rather
    // than re-spelled (the same reasoning BiquadResponse.h documents).
    return -rta::dsp::BiquadCascade::attenuationDb(std::span(&c, 1), w);
}

}  // namespace rta::eq
