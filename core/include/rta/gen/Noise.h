// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/gen/Prng.h"

#include <cstddef>
#include <span>

namespace rta::gen {

/// Default noise level. Capped at -12 dBFS RMS (not, say, -20 or -6) because
/// pink noise at this RMS still carries ~12 dB of crest factor (see
/// PinkNoise's crest-factor note below); a higher default would clip more
/// often, and headroom policy is deliberately not this class's job -- see
/// "No clipping" on PinkNoise.
inline constexpr double kDefaultNoiseLevelDbFsRms = -12.0;

/// Uniform white noise, RMS-referenced dBFS.
///
/// ## Level convention -- RMS, not peak
/// Noise is RMS-referenced (decision record docs/dsp/2026-08-27-generator.md,
/// "Level conventions" -- matches REW's documented behaviour), unlike the
/// deterministic generators (sine, sweep, MLS) which are peak-referenced,
/// because those have closed-form crest factors and peak hides nothing for
/// them, whereas noise's peak is an unbounded random variable and only RMS is
/// a stable, repeatable control.
///
/// `setLevelDbFsRms(d)` must make the long-run output RMS equal `10^(d/20)`.
/// `Pcg32::nextUniform()` draws Uniform[-1,1), which has RMS `sqrt(1/3)`
/// (derived in Prng.h, next to where the mapping is pinned -- re-derived here
/// too since it is load-bearing for this class's own correctness: variance of
/// Uniform[a,b) is `(b-a)^2/12`, so Uniform[-1,1) has variance `4/12 = 1/3`).
/// To turn a `1/sqrt(3)`-RMS stream into a `10^(d/20)`-RMS stream:
///
///     scale = 10^(d/20) / (1/sqrt(3)) = 10^(d/20) * sqrt(3)
///
/// ## Multichannel
/// This class owns exactly one `Pcg32`. There is no multichannel constructor
/// and none is needed: decorrelated multichannel is N separate `WhiteNoise`
/// instances, each built from `ChannelSeeds::forChannel(masterSeed, channel)`.
/// Correlated (duplicated) noise for mono-compatibility checks is likewise
/// just the caller feeding one instance's output to several channels -- no
/// core support required either way.
class WhiteNoise {
public:
    explicit WhiteNoise(Pcg32 rng, double levelDbFsRms = kDefaultNoiseLevelDbFsRms);

    void setLevelDbFsRms(double db) noexcept;

    [[nodiscard]] float nextSample() noexcept;
    void process(std::span<float> out) noexcept;

private:
    Pcg32 rng_;
    double levelDbFsRms_ = kDefaultNoiseLevelDbFsRms;
    double scale_ = 0.0;  ///< 10^(levelDbFsRms/20) * sqrt(3)
};

/// Pink noise: `WhiteNoise`'s raw uniform stream through Paul Kellett's
/// refined ("instrumentation grade") 7-term IIR, archived on musicdsp.org as
/// "pink noise filter" (music-dsp mailing list).
///
/// The coefficients below are an EMPIRICAL FIT -- Kellett tuned them to a
/// target 1/f magnitude response, they were not derived from pole placement
/// against a spectral target. The author's own stated accuracy bound is
/// **+/-0.05 dB above 9.2 Hz, measured at 44.1 kHz** -- that bound is NOT
/// re-derived here for this project's 48 kHz, which is exactly why the
/// independent +/-0.2 dB/oct slope test (core/tests/test_generator_noise.cpp,
/// "Pink noise falls at -3.01 dB per octave") exists: it is the thing that
/// actually verifies this filter at the sample rate this project runs at,
/// where the published bound was never claimed to apply. If the fitted table
/// is ever challenged, the recorded escalation is Kasdin's binomial-series
/// cascade, which has a published, derivable error recursion: N.J. Kasdin,
/// "Discrete Simulation of Colored Noise and Stochastic Processes and
/// 1/f^alpha Power Law Noise Generation", Proc. IEEE 83(5), 1995.
///
/// ## Level convention -- same RMS reference as WhiteNoise, corrected by the
/// filter's own gain
/// The SAME raw `nextUniform()` sample feeds the filter (not a pre-scaled
/// one), so the filter's own RMS gain for unit-variance input,
/// `kRmsGainVsWhite` (derived below), has to be divided out so the requested
/// dBFS lands on the filter's OUTPUT:
///
///     scale = 10^(d/20) * sqrt(3) / kRmsGainVsWhite
///
/// (the `sqrt(3)` un-normalises the raw uniform stream to unit RMS first,
/// exactly as in WhiteNoise; `kRmsGainVsWhite` then converts unit-RMS-in to
/// its corresponding RMS-out, so dividing by it converts a target RMS-out
/// back to the unit-RMS-in scale the filter expects).
///
/// ## No clipping, ever
/// This class never clips or clamps. Pink noise at the -12 dBFS RMS default
/// has roughly 12 dB of crest factor (see the crest-factor test), so peaks
/// will occasionally touch or exceed +/-1.0. Clamping inside `core` would
/// distort the spectrum the slope test measures and would hide the headroom
/// problem from whatever UI eventually displays it. Headroom management is
/// `app/`'s job, not this class's -- see CLAUDE.md's module boundaries.
///
/// ## Multichannel
/// Exactly like WhiteNoise: one `Pcg32` per instance, N instances (each from
/// `ChannelSeeds::forChannel`) for decorrelated channels, one instance's
/// output duplicated by the caller for correlated channels. No special
/// support here.
class PinkNoise {
public:
    /// The EXACT RMS gain of the Kellett filter for unit-variance white
    /// input. Not measured by running the filter -- derived algebraically,
    /// with the algebra spelled out below so a reader can check it with a
    /// pencil, then confirmed two further independent ways.
    ///
    /// ## Setup
    /// The filter is a parallel bank of 6 one-pole recursive paths
    /// `b_i[n] = p_i*b_i[n-1] + g_i*w[n]` (i = 0..5), plus a same-sample
    /// direct term `g_d*w[n]` and a one-sample-delayed direct term
    /// `g_e*w[n-1]` (held in `b6` from the previous sample):
    ///
    ///     out[n] = sum_i b_i[n]  +  g_d*w[n]  +  g_e*w[n-1]
    ///
    ///     i    p_i         g_i
    ///     0    0.99886     0.0555179
    ///     1    0.99332     0.0750759
    ///     2    0.96900     0.1538520
    ///     3    0.86650     0.3104856
    ///     4    0.55000     0.5329522
    ///     5   -0.76160    -0.0168980
    ///     g_d = 0.5362      (same-sample direct)
    ///     g_e = 0.115926    (one-sample-delayed direct)
    ///
    /// ## Steady-state second moments for w[n] i.i.d., unit variance
    /// (only the second moment of w matters for a LINEAR filter's output
    /// variance, so this holds for ANY unit-variance i.i.d. input, uniform or
    /// not -- the specific distribution never enters below):
    ///   - two one-pole paths:                 Cov(b_i, b_j) = g_i*g_j/(1-p_i*p_j)
    ///   - a one-pole path, same-sample input:  Cov(b_i[n], w[n])   = g_i
    ///   - a one-pole path, one-sample delayed:  Cov(b_i[n], w[n-1]) = g_i*p_i
    /// (the middle and last rows are exactly the two forms the class's
    /// implementer was told to use; both follow from expanding
    /// `b_i[n] = p_i*b_i[n-1] + g_i*w[n]` one step and using that `w` is
    /// serially uncorrelated).
    ///
    /// Expanding `Var(out)` over all 8 contributing paths (`Cov(w[n],w[n-1])
    /// = 0`, since white noise is serially uncorrelated):
    ///
    ///     Var(out) = sum_i sum_j g_i*g_j/(1-p_i*p_j)     (36 terms, i,j=0..5)
    ///              + g_d^2 + g_e^2
    ///              + 2*g_d*(sum_i g_i)
    ///              + 2*g_e*(sum_i g_i*p_i)
    ///
    /// ## Substituting the numbers (exact rational arithmetic; reproducible
    /// term-by-term from the table above with a calculator -- these are the
    /// five pieces the formula above sums):
    ///
    ///     sum_i g_i                          =  1.1109856
    ///     sum_i g_i*p_i                       =  0.854140589782
    ///     sum_i sum_j g_i*g_j/(1-p_i*p_j)      =  7.627519982186027...
    ///     g_d^2 + g_e^2                        =  0.300949277476
    ///     2*g_d*(sum_i g_i)                    =  1.19142095744
    ///     2*g_e*(sum_i g_i*p_i)                =  0.198034204022136264...
    ///     ---------------------------------------------------------
    ///     Var(out)                             =  9.317924421124163437...
    ///     RMS gain = sqrt(Var(out))            =  3.052527546333392785...
    ///
    /// Confirmed two further, independent ways: (1) numerically, via
    /// `(1/2*pi) * integral_0^2pi |H(e^jw)|^2 dw` for
    /// `H(z) = sum_i g_i/(1-p_i*z^-1) + g_d + g_e*z^-1`, trapezoid rule on a
    /// 2,000,001-point grid -- agrees with the closed form to 1.6e-14
    /// relative; (2) a 4,000,000-sample direct Monte-Carlo simulation of the
    /// exact recursion -- agrees within that run's own sampling error
    /// (~0.02%, consistent with `1/sqrt(N)` at this N). This value is
    /// GREATER than 1: the filter amplifies a unit-variance input by roughly
    /// 3x while reshaping its spectrum -- these coefficients were fitted for
    /// a spectral *shape*, not for unit gain, so there is no reason to expect
    /// attenuation here.
    ///
    /// Will be cross-checked again once `tools/gen_generator.py`'s own two
    /// independent derivations land in `core/tests/golden/generator.txt`'s
    /// `pink_rms_gain/value` row -- see test 15 in
    /// core/tests/test_generator_noise.cpp, which asserts this constant
    /// still agrees with that row to 1e-6 relative.
    static constexpr double kRmsGainVsWhite = 3.0525275463333927;

    explicit PinkNoise(Pcg32 rng, double levelDbFsRms = kDefaultNoiseLevelDbFsRms);

    void setLevelDbFsRms(double db) noexcept;

    [[nodiscard]] float nextSample() noexcept;
    void process(std::span<float> out) noexcept;

private:
    Pcg32 rng_;
    double levelDbFsRms_ = kDefaultNoiseLevelDbFsRms;
    double scale_ = 0.0;  ///< 10^(levelDbFsRms/20) * sqrt(3) / kRmsGainVsWhite

    // Filter state. double, zero-initialised: the golden fixture (test 14)
    // depends on both -- higher precision than the float output, and a
    // silent, known starting point.
    double b0_ = 0.0;
    double b1_ = 0.0;
    double b2_ = 0.0;
    double b3_ = 0.0;
    double b4_ = 0.0;
    double b5_ = 0.0;
    double b6_ = 0.0;
};

}  // namespace rta::gen
