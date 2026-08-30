// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace rta::gen {

/// Farina exponential sine sweep (ESS) plus its Farina-style inverse filter.
///
/// ## Where the closed form comes from
///
/// An ESS's instantaneous frequency rises exponentially with time:
/// `f(t) = f1 * exp(t/L)`. Requiring `f(T) = f2` gives `exp(T/L) = f2/f1`, i.e.
///
///     L = T / ln(f2/f1)                                    (lengthConstantL)
///
/// Phase is the integral of `2*pi*f(t) dt = 2*pi*f1*exp(t/L) dt`, which
/// integrates to `2*pi*f1*L*exp(t/L) + const`; choosing the constant so
/// `phase(0) = 0` gives
///
///     phase(t) = 2*pi*f1*L*(exp(t/L) - 1) = K*(exp(t/L) - 1)
///     K = 2*pi*f1*L                                        (phaseConstantK)
///
/// Sampled at `t = n/fs` this is exactly `phaseAt(n)` below. Because both L and
/// K are computed once from `(f1, f2, T)` and `phaseAt` evaluates the closed
/// form directly from the integer sample counter `n` -- never by accumulating a
/// running phase -- `restart()` is exact: recomputing from `n = 0` reproduces
/// the first pass bit-for-bit, and the generator cannot drift no matter how
/// long it runs. That is the real-time contract this class exists to keep.
///
/// ## Inverse filter, derived so its sign is checkable
///
/// An ESS spends equal *time* per octave, so its energy per octave is constant,
/// so its energy density per hertz falls as `1/f`: `|X(f)| ~ f^(-1/2)`, i.e.
/// **-3 dB/oct**. Inverting requires `|X(f)| * |Inv(f)| = 1` for every f in
/// range, so `|Inv(f)| ~ f^(+1/2)`, i.e. **+3 dB/oct**. Time reversal alone does
/// not change a signal's magnitude spectrum -- the reversed sweep still carries
/// the original `f^(-1/2)` shape -- so an amplitude ENVELOPE must be multiplied
/// onto the reversed sweep in the TIME domain to supply the missing `f^(+1)`
/// (twice +3 dB/oct = **+6 dB per octave**, exactly the number the decision
/// record states). For reversed index `m`, with original index `n = N-1-m`:
///
///     inv[m] = x[n] * (instantaneousFrequency(n) / endHz)
///
/// i.e. the already-rendered, already-faded sweep, reversed, with sample `m`
/// scaled by the ratio of its *original* instantaneous frequency to `endHz` --
/// so the envelope is naturally at its maximum (~1) at the very start of the
/// reversed signal (m = 0, n = N-1, where the original frequency was highest).
///
/// This envelope *decays with time* along the inverse signal (m increases,
/// n decreases, so instantaneousFrequency(n) decreases and the envelope
/// shrinks) while simultaneously *rising with frequency* (the frequency that
/// was PRESENT at original sample n, before reversal, is exactly what the
/// envelope tracks). "+6 dB/oct envelope" and "the envelope decays over time"
/// are the same signal described two ways -- a reader who has only met the
/// "decays over time" phrasing might "fix" the sign and break the
/// deconvolution. Don't: the decay-with-time and rise-with-frequency framings
/// are not in tension, they are the same fact.
class Sweep {
public:
    struct Config {
        double sampleRate    = 48000.0;
        double startHz       = 20.0;
        double endHz         = 20000.0;
        /// REQUESTED duration. The sweep actually rendered is a little
        /// shorter or longer -- between -1.0% and +1.3% across this project's
        /// configurations -- because `f1*L` is rounded to a whole number so
        /// that harmonic packets land on whole samples with whole-turn phase
        /// (Novak synchronisation; see the ctor). Read `durationSec()` or
        /// `lengthSamples()` for what you got; never assume this times
        /// `sampleRate`.
        double durationSec   = 10.0;
        double levelDbFsPeak = -6.0;   ///< peak-referenced: amplitude = 10^(db/20)
        double fadeInSec     = 0.02;   ///< a FLOOR, not the value: the widest of
                                       ///< this, 2/startHz, and fadeInOctaves
                                       ///< octaves of travel wins. See the ctor,
                                       ///< and fadeInOctavesAchieved() to read
                                       ///< back which one did.
        /// Minimum fade-in width in OCTAVES of sweep travel -- the unit that
        /// governs the deconvolution's pre-arrival artefact floor. Seconds do
        /// not: measured, 0.5 octave gives a -75.4 dB floor and 2 octaves gives
        /// -108.7 dB, and the figure does not depend on the sweep's duration.
        /// The `2/startHz` floor below is a CYCLES rule, so in octaves it
        /// SHRINKS as the sweep lengthens -- under it alone, a longer sweep
        /// measured worse. See docs/dsp/2026-08-30-sweep-ir-l4a.md decision 5.
        ///
        /// This is a trade-off the caller owns, not a setting with one right
        /// value. A wide fade-in tapers the bottom of the sweep: two octaves
        /// from 20 Hz shapes everything below 80 Hz, costing signal-to-noise
        /// where room modes live. To have both, set `startHz` two octaves BELOW
        /// the band of interest -- 5 Hz for a 20 Hz band -- which spends sweep
        /// duration instead of low-frequency energy. Set to 0.0 to restore the
        /// pre-2026-08-30 behaviour exactly.
        double fadeInOctaves = 2.0;

        double fadeOutSec    = 0.02;   ///< clamped up to 2/endHz -- two cycles at
                                       ///< the end frequency, the mirror of the
                                       ///< fade-in's floor. An unfaded
                                       ///< switch-off lands at the START of the
                                       ///< inverse filter, where the +6 dB/oct
                                       ///< envelope amplifies it.
    };

    /// Throws std::invalid_argument if sampleRate, startHz, durationSec are not
    /// positive, or endHz <= startHz (ln(f2/f1) must be positive for L to mean
    /// anything).
    explicit Sweep(const Config& config);

    [[nodiscard]] double lengthConstantL() const noexcept { return lengthL_; }
    [[nodiscard]] double phaseConstantK() const noexcept { return phaseK_; }
    [[nodiscard]] std::size_t lengthSamples() const noexcept { return lengthSamples_; }

    /// Samples spent on the raised-cosine start fade, after all three floors
    /// have competed. Exposed for the same reason phaseAt is: a clamp nobody
    /// can observe is a clamp nobody can check.
    [[nodiscard]] std::size_t fadeInSamples() const noexcept { return fadeInSamples_; }
    [[nodiscard]] std::size_t fadeOutSamples() const noexcept { return fadeOutSamples_; }

    /// The fade-in width actually achieved, in octaves of sweep travel, after
    /// all three floors have competed and the result has been rounded to whole
    /// samples. This is the number that predicts the artefact floor, so it is
    /// the one worth reading back.
    /// The duration actually rendered, after Novak synchronisation rounded
    /// `f1*L` to a whole number. Differs from `Config::durationSec` by up to
    /// about 1.3% in the configurations this project uses.
    [[nodiscard]] double durationSec() const noexcept;

    /// `f1 * L`, which synchronisation makes an integer. Exposed because a
    /// condition nobody can observe is a condition nobody can check.
    [[nodiscard]] double synchronisedCycles() const noexcept {
        return startHz_ * lengthL_;
    }

    [[nodiscard]] double fadeInOctavesAchieved() const noexcept;

    /// Lower edge of the band in which this sweep's deconvolution is
    /// meaningful: `f1 * exp(fadeInSec/L)`, the frequency the sweep had reached
    /// when the fade-in finished. When the octave floor is the binding one this
    /// collapses to `f1 * 2^fadeInOctaves` -- the lower band edge IS the knob.
    [[nodiscard]] double validBandLowHz() const noexcept;

    /// Upper edge: `f2 * exp(-fadeOutSec/L)`. Note the fade-out is deliberately
    /// NOT clamped in octaves: measured, a wider fade-out makes the artefact
    /// floor worse by about 3.5 dB per octave, the opposite of the fade-in.
    [[nodiscard]] double validBandHighHz() const noexcept;

    /// Closed-form phase at sample n, in radians: K*(exp(n/(fs*L)) - 1).
    /// Public because a definition nobody can call is a definition nobody can
    /// check -- same reasoning as BandWeights::responseAt
    /// (core/include/rta/dsp/BandWeights.h) in this same repo.
    [[nodiscard]] double phaseAt(std::size_t n) const noexcept;

    /// f(t) = f1 * exp(t/L), t = n/fs. The derivative of phaseAt/(2*pi) w.r.t.
    /// time, evaluated in closed form rather than by finite-differencing the
    /// phase (which would just reintroduce sampling error we already avoided).
    [[nodiscard]] double instantaneousFrequency(std::size_t n) const noexcept;

    /// Real-time safe: one std::exp and one std::sin, no allocation, no
    /// recursive state beyond the integer sample counter.
    float nextSample() noexcept;
    void  process(std::span<float> out) noexcept;

    /// Resets the sample counter to 0. Exact, no drift: phase is recomputed
    /// from n on every call, never accumulated, so this reproduces the first
    /// pass bit-for-bit.
    void restart() noexcept;

    /// NOT REAL-TIME SAFE. Allocates lengthSamples() floats and runs a full
    /// pass over the whole sweep. Build it on the message/control thread when
    /// parameters change and hand the resulting buffer to the analysis thread
    /// by pointer; NEVER call this from an audio callback -- it allocates and
    /// it is O(N). This is the rule that, if broken, produces dropouts at a
    /// live show, which is the exact situation this tool exists for.
    [[nodiscard]] std::vector<float> buildInverseFilter() const;

private:
    /// Raised-cosine gain in [0,1] at sample n of the rendered N-sample sweep:
    /// 0 at the very first/last sample of a fade region, 1 in the interior.
    [[nodiscard]] double fadeEnvelope(std::size_t n) const noexcept;

    double sampleRate_;
    double startHz_;
    double endHz_;
    double amplitude_;      ///< 10^(levelDbFsPeak/20)
    double lengthL_;        ///< T / ln(f2/f1)
    double phaseK_;         ///< 2*pi*f1*L
    std::size_t lengthSamples_;
    double durationSec_;    ///< as synchronised, not as requested
    double fadeOutSec_;     ///< as clamped, for validBandHighHz()
    std::size_t fadeInSamples_;
    std::size_t fadeOutSamples_;
    std::size_t n_ = 0;     ///< the only mutable state nextSample() advances
};

}  // namespace rta::gen
