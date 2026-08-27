// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/ButterworthDesign.h"

// The design chain, expanded step by step, is written out in
// docs/plans/2026-08-27-filterbank-impl-plan.md section 1. Each free function
// below implements exactly one of its five numbered steps, in the same order,
// so the chain can be checked against that document line by line.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace rta::dsp {

namespace {

using Complex = std::complex<double>;

void validateInputs(double lowerHz, double upperHz, double sampleRate, int sections) {
    if (sections < 1 || sections > 12) {
        throw std::invalid_argument("ButterworthDesign: sections must be in 1..12");
    }
    if (!(sampleRate > 0.0)) {
        throw std::invalid_argument("ButterworthDesign: sampleRate must be positive");
    }
    if (!(lowerHz > 0.0)) {
        throw std::invalid_argument("ButterworthDesign: lowerHz must be positive");
    }
    if (!(upperHz > lowerHz)) {
        throw std::invalid_argument("ButterworthDesign: needs lowerHz < upperHz");
    }
}

/// Step 1: buttap. p_k = -exp(j*pi*(2k-N-1)/(2N)), k = 1..N. No zeros, gain 1.
/// For even N (this bank uses N=6) every pole is complex; the exponent never
/// hits 0 or pi, so there is no real prototype pole to special-case.
std::vector<Complex> buttapPoles(int n) {
    std::vector<Complex> poles;
    poles.reserve(static_cast<std::size_t>(n));
    for (int k = 1; k <= n; ++k) {
        const double theta =
            std::numbers::pi * (2.0 * k - n - 1) / (2.0 * n);
        poles.push_back(-std::exp(Complex(0.0, theta)));
    }
    return poles;
}

/// Step 2: prewarp. The bilinear transform maps the analog axis through a
/// tangent, so an edge specified at f in hertz is pushed to the analog
/// frequency that lands back on f after the transform.
double prewarp(double hz, double sampleRate) {
    return 2.0 * sampleRate * std::tan(std::numbers::pi * hz / sampleRate);
}

/// Step 3: lp2bp_zpk. The prototype has no zeros, so the band-pass gets N
/// zeros at the origin; each prototype pole becomes a +/- pair.
Zpk lowPassToBandPass(const std::vector<Complex>& protoPoles, double w0, double bw) {
    Zpk out;
    out.zeros.assign(protoPoles.size(), Complex(0.0, 0.0));
    out.poles.reserve(protoPoles.size() * 2);

    for (const Complex& p : protoPoles) {
        const Complex s = p * (bw / 2.0);
        const Complex disc = std::sqrt(s * s - w0 * w0);
        out.poles.push_back(s + disc);
        out.poles.push_back(s - disc);
    }
    out.gain = std::pow(bw, static_cast<double>(protoPoles.size()));
    return out;
}

/// Step 4: bilinear_zpk with fs2 = 2*fs. Degree = #poles - #zeros = N zeros
/// are appended at z = -1, the image of s = infinity.
Zpk bilinear(const Zpk& analog, double sampleRate) {
    const double fs2 = 2.0 * sampleRate;

    Zpk out;
    out.zeros.reserve(analog.poles.size());
    for (const Complex& z : analog.zeros) out.zeros.push_back((fs2 + z) / (fs2 - z));

    out.poles.reserve(analog.poles.size());
    for (const Complex& p : analog.poles) out.poles.push_back((fs2 + p) / (fs2 - p));

    const std::size_t degree = analog.poles.size() - analog.zeros.size();
    for (std::size_t i = 0; i < degree; ++i) out.zeros.emplace_back(-1.0, 0.0);

    Complex numProd(1.0, 0.0), denProd(1.0, 0.0);
    for (const Complex& z : analog.zeros) numProd *= (fs2 - z);
    for (const Complex& p : analog.poles) denProd *= (fs2 - p);
    out.gain = analog.gain * (numProd / denProd).real();

    return out;
}

/// One pole/zero list split into conjugate-pair representatives (positive
/// imaginary part, sorted by real part then imaginary part) and real roots
/// (sorted ascending) -- scipy's _cplxreal, for the shapes this chain
/// actually produces (see plan section 1 step 5.1).
struct ConjugateSplit {
    std::vector<Complex> complexReps;
    std::vector<double>  reals;
};

/// tol = 1e-10 * max(1, |z|): matches _cplxreal's default (100*eps, scaled by
/// magnitude) closely enough to be a safety net rather than a load-bearing
/// constant. Poles produced by step 4 come out in exact conjugate pairs when
/// the arithmetic is done symmetrically -- see plan section 4.3.
constexpr double kConjugateTolerance = 1.0e-10;

ConjugateSplit splitConjugateReal(std::vector<Complex> values) {
    std::sort(values.begin(), values.end(), [](const Complex& a, const Complex& b) {
        if (a.real() != b.real()) return a.real() < b.real();
        return std::abs(a.imag()) < std::abs(b.imag());
    });

    ConjugateSplit split;
    std::vector<Complex> complexVals;
    for (const Complex& v : values) {
        const double scale = std::max(1.0, std::abs(v));
        if (std::abs(v.imag()) <= kConjugateTolerance * scale) {
            split.reals.push_back(v.real());
        } else {
            complexVals.push_back(v);
        }
    }
    std::sort(split.reals.begin(), split.reals.end());

    std::vector<Complex> pos, neg;
    for (const Complex& v : complexVals) {
        (v.imag() > 0.0 ? pos : neg).push_back(v);
    }
    if (pos.size() != neg.size()) {
        throw std::logic_error("ButterworthDesign: complex value with no matching conjugate");
    }

    std::vector<bool> negUsed(neg.size(), false);
    split.complexReps.reserve(pos.size());
    for (const Complex& p : pos) {
        std::size_t bestJ = 0;
        double bestDist = 0.0;
        bool found = false;
        for (std::size_t j = 0; j < neg.size(); ++j) {
            if (negUsed[j]) continue;
            const double dist = std::abs(p - std::conj(neg[j]));
            if (!found || dist < bestDist) { bestDist = dist; bestJ = j; found = true; }
        }
        const double scale = std::max(1.0, std::abs(p));
        if (!found || bestDist > kConjugateTolerance * scale) {
            throw std::logic_error("ButterworthDesign: complex value with no matching conjugate");
        }
        negUsed[bestJ] = true;
        split.complexReps.push_back((p + std::conj(neg[bestJ])) / 2.0);
    }

    std::sort(split.complexReps.begin(), split.complexReps.end(),
              [](const Complex& a, const Complex& b) {
                  if (a.real() != b.real()) return a.real() < b.real();
                  return a.imag() < b.imag();
              });
    return split;
}

}  // namespace

Zpk ButterworthDesign::bandPassZpk(double lowerHz, double upperHz, double sampleRate,
                                   int sections) {
    validateInputs(lowerHz, upperHz, sampleRate, sections);

    const auto protoPoles = buttapPoles(sections);

    const double wLo = prewarp(lowerHz, sampleRate);
    const double wHi = prewarp(upperHz, sampleRate);
    const double bw = wHi - wLo;
    const double w0 = std::sqrt(wLo * wHi);

    const Zpk analogBandPass = lowPassToBandPass(protoPoles, w0, bw);
    return bilinear(analogBandPass, sampleRate);
}

std::vector<Biquad::Coeffs> ButterworthDesign::pairIntoSections(const Zpk& zpk) {
    // For every band-pass this chain designs, the pole list reduces to N
    // conjugate-pair representatives and nothing real, and the zero list
    // reduces to 2N reals (N copies of -1, N copies of +1) and nothing
    // complex -- see plan section 1 step 5.1. A shape outside that is a bug
    // upstream, not something to silently reinterpret.
    const ConjugateSplit poleSplit = splitConjugateReal(zpk.poles);
    const ConjugateSplit zeroSplit = splitConjugateReal(zpk.zeros);

    if (!poleSplit.reals.empty() || !zeroSplit.complexReps.empty()) {
        throw std::logic_error(
            "ButterworthDesign::pairIntoSections: expected all-complex poles and "
            "all-real zeros, per the band-pass design chain");
    }

    const std::size_t n = poleSplit.complexReps.size();
    if (zeroSplit.reals.size() != 2 * n) {
        throw std::logic_error("ButterworthDesign::pairIntoSections: pole/zero count mismatch");
    }

    std::vector<Complex> poles = poleSplit.complexReps;      // sorted real, then imag
    std::vector<double>  zeros = zeroSplit.reals;             // sorted ascending
    std::vector<bool>    poleUsed(n, false);
    std::vector<bool>    zeroUsed(zeros.size(), false);
    std::vector<Biquad::Coeffs> sections(n);

    // Fill from the LAST index down to the first: each step takes the
    // remaining pole closest to the unit circle, so the section that ends up
    // at index 0 is whichever pole is left last -- the one FARTHEST from the
    // unit circle. That is what makes the sections come out in ascending pole
    // radius. See plan section 1 step 5.2-5.6.
    for (std::size_t slot = n; slot-- > 0;) {
        std::size_t bestPole = 0;
        double bestPoleVal = 0.0;
        bool foundPole = false;
        for (std::size_t i = 0; i < n; ++i) {
            if (poleUsed[i]) continue;
            const double val = std::abs(1.0 - std::abs(poles[i]));
            if (!foundPole || val < bestPoleVal) { bestPoleVal = val; bestPole = i; foundPole = true; }
        }
        poleUsed[bestPole] = true;
        const Complex p1 = poles[bestPole];
        const Complex p2 = std::conj(p1);

        double z1 = 0.0, z2 = 0.0;
        for (int pick = 0; pick < 2; ++pick) {
            std::size_t bestZero = 0;
            double bestZeroDist = 0.0;
            bool foundZero = false;
            for (std::size_t i = 0; i < zeros.size(); ++i) {
                if (zeroUsed[i]) continue;
                const double dist = std::abs(Complex(zeros[i], 0.0) - p1);
                if (!foundZero || dist < bestZeroDist) { bestZeroDist = dist; bestZero = i; foundZero = true; }
            }
            zeroUsed[bestZero] = true;
            (pick == 0 ? z1 : z2) = zeros[bestZero];
        }

        Biquad::Coeffs c;
        c.b0 = 1.0;
        c.b1 = -(z1 + z2);
        c.b2 = z1 * z2;
        c.a1 = -(p1 + p2).real();
        c.a2 = (p1 * p2).real();
        sections[slot] = c;
    }

    // The whole gain lands in section 0 -- the lowest-radius, best-conditioned
    // section, not distributed evenly.
    sections[0].b0 *= zpk.gain;
    sections[0].b1 *= zpk.gain;
    sections[0].b2 *= zpk.gain;

    return sections;
}

ButterworthDesign::Result ButterworthDesign::bandPass(double lowerHz, double upperHz,
                                                       double sampleRate, int sections) {
    validateInputs(lowerHz, upperHz, sampleRate, sections);

    // Section 2 of the plan: an edge that would reach Nyquist is clamped
    // rather than handed to the design chain, which would otherwise either
    // reject it (buttap-derived butter() raises) or, worse, silently design an
    // unstable filter from a warped-negative frequency.
    const double nyquistLimit = kNyquistEdgeFraction * (sampleRate / 2.0);
    const bool clamped = upperHz > nyquistLimit;
    const double designedUpper = clamped ? nyquistLimit : upperHz;
    if (!(designedUpper > lowerHz)) {
        throw std::invalid_argument("ButterworthDesign: clamped upper edge is at or below lowerHz");
    }

    const Zpk zpk = bandPassZpk(lowerHz, designedUpper, sampleRate, sections);

    Result result;
    result.sections = pairIntoSections(zpk);
    result.lowerHz = lowerHz;
    result.upperHz = designedUpper;
    result.nyquistClamped = clamped;
    result.maxPoleRadius = BiquadCascade(result.sections).maxPoleRadius();

    return result;
}

}  // namespace rta::dsp
