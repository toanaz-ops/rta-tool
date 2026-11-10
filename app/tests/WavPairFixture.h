// SPDX-License-Identifier: AGPL-3.0-or-later
// H12 T3 fixtures: a reference file and a measurement file recorded on a clock
// that is off by eps and whose recording began b samples EARLIER than the
// reference's (b samples of pre-roll: reference sample 0 is measurement sample b).
//
// INDEPENDENCE FROM THE CODE UNDER TEST. The measurement is NOT made with
// rta::dsp::KaiserSincInterpolator (that would test the resampler against
// itself: any kernel error would cancel). It is made here with a windowed sinc
// of +-256 taps and a DIFFERENT window (4-term Blackman-Harris), in double.
//
// ACCURACY OF THIS REFERENCE INTERPOLATOR (checked on sinusoids in
// test_wav_pair_transfer_limits.cpp rather than assumed). The kernel is
// sinc(x) w(x/256). The Blackman-Harris 4-term window has side lobes <= -92 dB
// (Harris 1978), i.e. 2.5e-5, and a main lobe of +-4 bins of 1/512 cycles per
// sample = +-0.0078 fs. A sinusoid at f <= 0.45 fs is 0.05 fs = 6.4 main-lobe
// half-widths away from the sinc's cut-off at 0.5 fs, so the passband error is
// the window's side-lobe level: <= 3e-5 per sample, the figure the test asserts
// with a 1e-4 margin. T1's Kaiser kernel (+-32 taps, A = 100 dB) is 1e-5: this
// reference is NOT more accurate than T1 in its passband by an order of
// magnitude, but it is a different kernel on a different window with 8x the
// taps, so a defect in either shows as a difference between them.
#pragma once

#include "WavTransferFixture.h"

#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <vector>

namespace wavpair {

using wavfix::kFs;
using wavfix::MemorySource;

constexpr int kRefHalfLength = 256;

/// y(t) by a +-256-tap Blackman-Harris-windowed sinc. Samples outside [0, size)
/// are zero (the signal begins at rest). t is a position in samples.
inline double sincInterpolate(const std::vector<double>& y, double t) {
    const double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
    const double base = std::floor(t);
    const double frac = t - base;
    const auto b = static_cast<std::ptrdiff_t>(base);
    const auto size = static_cast<std::ptrdiff_t>(y.size());
    if (frac == 0.0) return (b >= 0 && b < size) ? y[static_cast<std::size_t>(b)] : 0.0;
    const double pi = std::numbers::pi;
    const double sinFrac = std::sin(pi * frac);
    // Tap i = b - 255 .. b + 256, x_i = frac + (b - i) runs +255+frac .. frac-256.
    // sin(pi x_i) = (-1)^(b-i) sin(pi frac). The window angle pi x_i / 256 steps
    // by -pi/256 per tap: a unit-phasor rotation instead of a cos per tap.
    const std::complex<double> step = std::polar(1.0, -pi / kRefHalfLength);
    std::complex<double> z = std::polar(1.0, pi * (frac + (kRefHalfLength - 1)) / kRefHalfLength);
    double acc = 0.0;
    double x = frac + (kRefHalfLength - 1);
    double sign = ((kRefHalfLength - 1) % 2 == 0) ? 1.0 : -1.0;  // (-1)^(b - i), b - i = 255 first
    for (int k = 0; k < 2 * kRefHalfLength; ++k) {
        const std::ptrdiff_t i = b - (kRefHalfLength - 1) + k;
        if (i >= 0 && i < size) {
            const double c1 = z.real();
            const double w = a0 + a1 * c1 + a2 * (2.0 * c1 * c1 - 1.0) + a3 * (4.0 * c1 * c1 * c1 - 3.0 * c1);
            acc += y[static_cast<std::size_t>(i)] * sign * sinFrac / (pi * x) * w;
        }
        z *= step;
        x -= 1.0;
        sign = -sign;
        if ((k & 127) == 127) {  // re-anchor the recurrence: drift is ~1e-14, this is belt and braces
            z = std::polar(1.0, pi * (frac + (kRefHalfLength - 1) - (k + 1)) / kRefHalfLength);
        }
    }
    return acc;
}

/// The test system's output in double (the same +6 dB / 1 kHz / Q = 1 biquad
/// the one-file fixture uses), from rest.
inline std::vector<double> systemOutput(const std::vector<float>& x) {
    rta::dsp::BiquadCascade cascade({wavfix::peaking()});
    std::vector<double> y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) y[i] = cascade.processSample(x[i]);
    return y;
}

struct Pair {
    MemorySource reference;    ///< one channel
    MemorySource measurement;  ///< one channel
};

/// The system the pair is measured through, in double, from rest.
using System = std::vector<double> (*)(const std::vector<float>&);

inline std::vector<double> flatSystem(const std::vector<float>& x) { return {x.begin(), x.end()}; }

namespace lr4 {

using Cascade = rta::dsp::BiquadCascade;

/// RBJ cookbook 2nd-order Butterworth (Q = 1/sqrt 2) low-pass / high-pass at fc.
inline rta::dsp::Biquad::Coeffs butterworth(bool highPass, double fc) {
    const double w0 = 2.0 * std::numbers::pi * fc / kFs, c = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * std::numbers::sqrt2 / 2.0);
    const double a0 = 1.0 + alpha;
    const double sign = highPass ? 1.0 : -1.0;
    const double b0 = (1.0 + sign * c) / 2.0 / a0, b1 = -sign * (1.0 + sign * c) / a0;
    return {b0, b1, b0, -2.0 * c / a0, (1.0 - alpha) / a0};
}

/// y = LP4(x) + HP4(x), each 4th order Linkwitz-Riley = two cascaded Butterworth biquads. The sum
/// of the two is flat in magnitude (Linkwitz & Riley 1976) and all-pass in phase: 360 degrees over
/// the band, group delay 2/(Q w0) = 216 samples at 100 Hz, 15 at 1500 Hz. The test never trusts that
/// identity: it measures the magnitude and compares it to 0 dB.
inline std::vector<double> through(const std::vector<float>& x, double fc) {
    Cascade lp({butterworth(false, fc), butterworth(false, fc)});
    Cascade hp({butterworth(true, fc), butterworth(true, fc)});
    std::vector<double> y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) y[i] = lp.processSample(x[i]) + hp.processSample(x[i]);
    return y;
}

}  // namespace lr4

inline std::vector<double> lr4At100(const std::vector<float>& x) { return lr4::through(x, 100.0); }
inline std::vector<double> lr4At1500(const std::vector<float>& x) { return lr4::through(x, 1500.0); }

/// reference[n] = x[n]; measurement[m] = y(tau(m)) with y = system(x). `tau`
/// maps a measurement sample index to the time (in reference samples) it sees.
/// `x` is generated long enough that tau(measLength) never runs off its end.
template <class Tau>
Pair makePairThrough(std::size_t refLength, std::size_t measLength, Tau tau, System system,
                     std::uint32_t seed = 11) {
    const auto need = static_cast<std::size_t>(std::ceil(tau(static_cast<double>(measLength)))) + 600;
    const auto x = wavfix::pink(std::max(need, refLength), seed);
    const auto y = system(x);
    Pair p;
    p.reference.channels.assign(1, std::vector<float>(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(refLength)));
    p.measurement.channels.assign(1, std::vector<float>(measLength));
    for (std::size_t m = 0; m < measLength; ++m) {
        p.measurement.channels[0][m] = static_cast<float>(sincInterpolate(y, tau(static_cast<double>(m))));
    }
    return p;
}

template <class Tau>
Pair makePair(std::size_t refLength, std::size_t measLength, Tau tau, std::uint32_t seed = 11,
              bool throughSystem = true) {
    return makePairThrough(refLength, measLength, tau, throughSystem ? System(systemOutput) : System(flatSystem),
                           seed);
}

/// Meas sample m sees y((m - b)/(1 + eps)): the measurement clock is `eps` fast
/// and its recording began b samples BEFORE the reference's (meas sample 0 sees
/// reference time -b / (1 + eps), i.e. b samples of pre-roll). Reference sample
/// n therefore sits at measurement position t(n) = b + n (1 + eps), and a
/// recorder wall-clock stamp (bext TimeReference) for the measurement is the
/// reference's MINUS b.
inline auto linearClock(double bSamples, double eps) {
    return [=](double m) { return (m - bSamples) / (1.0 + eps); };
}

/// Two segments with different clock errors, joined continuously at ref sample `bendAt`.
inline auto bentClock(double bSamples, double eps1, double eps2, double bendAt) {
    const double tBend = bSamples + bendAt * (1.0 + eps1);  // measurement position of the bend
    return [=](double m) {
        return m <= tBend ? (m - bSamples) / (1.0 + eps1) : bendAt + (m - tBend) / (1.0 + eps2);
    };
}

/// Integer-shifted copy (no interpolation anywhere): meas[m] = y[m - shift].
inline Pair shiftedPair(std::size_t refLength, std::size_t measLength, std::ptrdiff_t shift,
                        std::uint32_t seed = 11) {
    return makePair(refLength, measLength, [=](double m) { return m - static_cast<double>(shift); }, seed);
}

/// Truth for a linearClock pair: reference sample n sits at measurement position
/// t(n) = b + n (1 + eps), so the delay (meas minus ref) at n is b + eps n.
inline double trueDelayAt(double bSamples, double eps, double n) { return bSamples + eps * n; }

}  // namespace wavpair
