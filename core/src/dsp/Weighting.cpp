// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/Weighting.h"

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace rta::dsp {

namespace {

// IEC 61672-1 Annex E pole frequencies, hertz, verbatim.
constexpr double kF1 = 20.598997;
constexpr double kF2 = 107.65265;
constexpr double kF3 = 737.86223;
constexpr double kF4 = 12194.217;

constexpr double kTwoPi = 2.0 * std::numbers::pi;

/// The A-weighting rational function in f (Hz), gain = 1: numerator f^4 is
/// un-square-rooted because f1/f4 are DOUBLE poles (the square already comes
/// from squaring |s+w|), while f2/f3 are single poles and stay under a
/// square root. Getting this backwards still looks like A-weighting, which
/// is exactly why it's worth stating in a comment instead of just code.
double bracketA(double f) noexcept {
    const double f2 = f * f;
    return (f2 * f2) /
           ((f2 + kF1 * kF1) * std::sqrt(f2 + kF2 * kF2) * std::sqrt(f2 + kF3 * kF3) *
            (f2 + kF4 * kF4));
}

/// The C-weighting rational function in f (Hz), gain = 1.
double bracketC(double f) noexcept {
    const double f2 = f * f;
    return f2 / ((f2 + kF1 * kF1) * (f2 + kF4 * kF4));
}

/// Analog gain that makes the f-domain bracket read 1 at 1 kHz -- i.e. the
/// normalisation IEC 61672-1 Annex E specifies, computed rather than typed
/// in so a mistyped pole cannot silently survive as a "close enough" gain.
double analogGainA() noexcept { return 1.0 / bracketA(1000.0); }
double analogGainC() noexcept { return 1.0 / bracketC(1000.0); }

/// Bilinear transform of a real analog pole at s = -w (equivalently a zero
/// at s = -w, same formula): z = (fs2 + s) / (fs2 - s) evaluated at s = -w.
double bilinear(double w, double fs2) noexcept { return (fs2 - w) / (fs2 + w); }

/// Builds the 3-section A cascade. Structural derivation (see the decision
/// record and the implementation plan section 4.2): A has a quadruple analog
/// zero at s=0 (-> 4 digital zeros at z=+1) and 6 poles (double at w1, w4;
/// single at w2, w3), a degree deficit of 2 that places 2 more zeros at
/// z=-1. Because radius(w) is strictly decreasing in w for w>0 at any audio
/// sample rate, and w4 > w3 > w2 > w1, the bilinear pole radii satisfy
/// radius(w4) < radius(w3) < radius(w2) < radius(w1) always -- so the
/// section structure and ascending-radius order below are sample-rate
/// independent, not just true at the rates this file happens to test.
std::vector<Biquad::Coeffs> designA(double sampleRate) {
    const double fs2 = 2.0 * sampleRate;
    const double w1 = kTwoPi * kF1, w2 = kTwoPi * kF2, w3 = kTwoPi * kF3, w4 = kTwoPi * kF4;

    // Analog s-domain zpk gain. |H_analog(j*2*pi*f)| = kZpk/(2*pi)^2 * bracketA(f)
    // (the (2*pi)^2 falls out of converting the f-domain bracket, built from
    // |s+w_i| = 2*pi*sqrt(f^2+f_i^2), back to the s-domain pole/zero product;
    // the exponent is always (2*pi)^2 because A has 6 poles and 4 zeros, a
    // fixed degree difference of 2). Requiring unity at 1 kHz gives this.
    const double kZpk = analogGainA() * kTwoPi * kTwoPi;

    const double p4 = bilinear(w4, fs2);
    const double p1 = bilinear(w1, fs2);
    const double p2 = bilinear(w2, fs2);
    const double p3 = bilinear(w3, fs2);

    // k_digital = k * real(prod(fs2 - z_i) / prod(fs2 - p_i)) over the
    // ORIGINAL (un-padded) zeros/poles: 4 zeros at s=0 contribute fs2^4;
    // poles at s=-w_i contribute (fs2 - (-w_i)) = (fs2 + w_i) each.
    const double kDigital =
        kZpk * (fs2 * fs2 * fs2 * fs2) /
        ((fs2 + w4) * (fs2 + w4) * (fs2 + w1) * (fs2 + w1) * (fs2 + w2) * (fs2 + w3));

    std::vector<Biquad::Coeffs> sections(3);
    // Smallest radius: double pole at w4, paired with the 2 degree-deficit
    // zeros at z=-1 (raw numerator (1,2,1)). The overall gain is folded into
    // this section's numerator only, per the digitisation chain.
    sections[0] = {kDigital * 1.0, kDigital * 2.0, kDigital * 1.0, -2.0 * p4, p4 * p4};
    // Middle radius: the two DISTINCT single poles at w2, w3 combined into
    // one quadratic denominator, paired with 2 of the zeros at z=+1.
    sections[1] = {1.0, -2.0, 1.0, -(p2 + p3), p2 * p3};
    // Largest radius (closest to the unit circle): double pole at w1,
    // paired with the remaining 2 zeros at z=+1.
    sections[2] = {1.0, -2.0, 1.0, -2.0 * p1, p1 * p1};
    return sections;
}

/// Builds the 2-section C cascade. Same structure as A minus the w2/w3
/// section: C has a double analog zero at s=0 (-> 2 zeros at z=+1) and
/// double poles at w1, w4 only, degree deficit 2 (-> 2 zeros at z=-1).
std::vector<Biquad::Coeffs> designC(double sampleRate) {
    const double fs2 = 2.0 * sampleRate;
    const double w1 = kTwoPi * kF1, w4 = kTwoPi * kF4;

    const double kZpk = analogGainC() * kTwoPi * kTwoPi;
    const double p4 = bilinear(w4, fs2);
    const double p1 = bilinear(w1, fs2);

    const double kDigital =
        kZpk * (fs2 * fs2) / ((fs2 + w4) * (fs2 + w4) * (fs2 + w1) * (fs2 + w1));

    std::vector<Biquad::Coeffs> sections(2);
    sections[0] = {kDigital * 1.0, kDigital * 2.0, kDigital * 1.0, -2.0 * p4, p4 * p4};
    sections[1] = {1.0, -2.0, 1.0, -2.0 * p1, p1 * p1};
    return sections;
}

}  // namespace

std::string_view toString(WeightingType type) noexcept {
    switch (type) {
        case WeightingType::A: return "A";
        case WeightingType::C: return "C";
        case WeightingType::Z: return "Z";
    }
    return "?";
}

Weighting::Weighting(WeightingType type, double sampleRate)
    : type_(type), sampleRate_(sampleRate) {
    if (!(sampleRate_ > 0.0)) {
        throw std::invalid_argument("Weighting: sampleRate must be > 0");
    }
    switch (type_) {
        case WeightingType::A: cascade_ = BiquadCascade(designA(sampleRate_)); break;
        case WeightingType::C: cascade_ = BiquadCascade(designC(sampleRate_)); break;
        case WeightingType::Z: cascade_ = BiquadCascade();  break;  // flat: zero sections
    }
}

double Weighting::analyticDb(double frequencyHz, WeightingType type) noexcept {
    switch (type) {
        case WeightingType::A: return 20.0 * std::log10(analogGainA() * bracketA(frequencyHz));
        case WeightingType::C: return 20.0 * std::log10(analogGainC() * bracketC(frequencyHz));
        case WeightingType::Z: return 0.0;
    }
    return 0.0;
}

double Weighting::responseDb(double frequencyHz) const noexcept {
    const double omega = kTwoPi * frequencyHz / sampleRate_;  // 2*pi*f/fs, rad/sample
    return -cascade_.attenuationDb(omega);
}

}  // namespace rta::dsp
