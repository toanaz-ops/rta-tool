// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace rta::ir {

/// The result of deconvolving a captured sweep response.
///
/// ## Nothing is removed, and that is deliberate
///
/// Müller & Massarani advise chopping the negative-time half away. That is
/// right for room acoustics and wrong here, because for this project that
/// region IS the distortion measurement. The Nth harmonic of an exponential
/// sweep has, at time t, the instantaneous frequency the fundamental had at
/// `t + L*ln(N)`; deconvolution maps the fundamental at t onto zero, so it maps
/// that harmonic onto `-L*ln(N)`. Discarding the region would make a later lane
/// re-measure what was already captured.
///
/// The packet POSITIONS follow from that closed form. Their PHASE does not,
/// unless the sweep satisfies Novak's synchronisation condition (`f1*L` an
/// integer). Amplitude-domain distortion analysis is unaffected; phase-accurate
/// per-harmonic analysis is not. See
/// `docs/dsp/2026-08-30-sweep-ir-l4a.md` decision 3.
struct Deconvolution {
    /// The whole linear convolution, length `response + inverse - 1`.
    std::vector<float> samples;

    /// Index at which `t = 0` sits: `inverseFilter.size() - 1`. Everything
    /// below it is negative time.
    std::size_t originIndex = 0;

    double sampleRate = 0.0;

    /// The scalar already applied to `samples`, so a reader can undo it.
    double normalisationGain = 1.0;

    /// `L = T / ln(f2/f1)` for the sweep that produced this, in seconds.
    /// Carried so a later lane can locate the harmonic packets without needing
    /// the `Sweep` object, which by then may not exist.
    double harmonicSpacingL = 0.0;

    /// The band the excitation covered AT ALL -- the sweep's `startHz`/`endHz`.
    /// Zero means "not stated"; a reader must then not clamp rather than assume
    /// a full band.
    ///
    /// Carried because a spectrum measured from this result contains energy
    /// outside the excited band that is NOT response: leakage from windowing,
    /// and the inverse filter's own fade. A band-edge estimator that counts
    /// those bins reports a system wider than the one measured -- a `butter(2)`
    /// section designed 7.5-120 Hz reads 4.09 octaves by counting a bin at
    /// 11.7 Hz, when only 20-120 Hz was ever excited. See
    /// `docs/dsp/2026-08-30-sweep-ir-l4a.md` decision 6b, estimator defect 2.
    double excitationLowHz = 0.0;
    double excitationHighHz = 0.0;

    /// The narrower band in which this measurement is TRUSTWORTHY --
    /// `Sweep::validBandLowHz()` / `validBandHighHz()`, i.e. after the fades.
    /// Zero means "not stated".
    ///
    /// Distinct from the excitation band on purpose, and the distinction is
    /// load-bearing rather than pedantic. Clamping a band-edge search to THIS
    /// band instead of the excitation band makes a system lying below it
    /// unreadable: a 50-71 Hz subwoofer measured with a sweep whose fade-in
    /// finishes at 80 Hz then reports a low edge of 82 Hz, because the -10 dB
    /// threshold is taken against a peak made of skirt. The number is not
    /// merely imprecise, it describes a different loudspeaker.
    ///
    /// What this band IS for: deciding whether the measurement can support a
    /// judgement at all. A verdict whose threshold sits outside it is being
    /// asked of a sweep that never established the answer.
    double trustedLowHz = 0.0;
    double trustedHighHz = 0.0;

    /// Offset in samples from `originIndex` to the Nth harmonic packet:
    /// `-L*ln(N)*fs`, hence NEGATIVE for every order >= 2. Returns 0 for order
    /// 1 -- the fundamental IS the origin -- and for anything below 1.
    [[nodiscard]] double harmonicOffsetSamples(int order) const noexcept;
};

struct DeconvolverConfig {
    double sampleRate = 0.0;
    double harmonicSpacingL = 0.0;

    /// Applied to every output sample. Compute it with `inBandNormalisation`
    /// from a reference deconvolution; 1.0 leaves the result raw.
    double normalisationGain = 1.0;

    /// Copied into the result. Leave at zero when not known; nothing downstream
    /// may then clamp to it. NOT validated here: this function's contract is
    /// about lengths and sample rate, and widening it would reject callers who
    /// legitimately do not know the band.
    double excitationLowHz = 0.0;
    double excitationHighHz = 0.0;
    double trustedLowHz = 0.0;
    double trustedHighHz = 0.0;
};

/// Linear (non-cyclic) deconvolution: `response` convolved with
/// `inverseFilter`, zero-padded so no wrap-around occurs.
///
/// **`inverseFilter` is an argument, never constructed here.** Today the caller
/// passes `rta::gen::Sweep::buildInverseFilter()`. Müller prefers inverting a
/// *measured* loopback reference instead, which needs hardware this project
/// does not yet have; when it does, adopting the better method must be a change
/// of argument, not a rewrite of this function. That is why this file does not
/// include `rta/gen/Sweep.h`, and must not start.
///
/// NOT real-time safe: it allocates, and it is O(N log N) over the whole
/// capture. A 10 s sweep against a 12 s capture needs a 2^21 transform.
///
/// Throws `std::invalid_argument` if either span is empty, if `sampleRate` is
/// not positive, or if `response` is shorter than `inverseFilter` -- the origin
/// would then fall outside the data, and returning a struct whose
/// `originIndex` indexes nothing is worse than refusing.
[[nodiscard]] Deconvolution deconvolve(std::span<const float> response,
                                       std::span<const float> inverseFilter,
                                       const DeconvolverConfig& config);

/// How far the magnitude spectrum departs from its own in-band mean, in dB.
struct BandFlatness {
    double minDb = 0.0;
    double maxDb = 0.0;
};

/// The scalar that makes a unity path read 0 dB across `[lowHz, highHz]`.
///
/// Pass the REFERENCE deconvolution -- the excitation deconvolved with its own
/// inverse filter -- and the band edges the sweep reports. The returned gain
/// then belongs in `DeconvolverConfig::normalisationGain` for the measurement.
///
/// Uses the MEAN in-band magnitude, not the peak. The peak height depends on
/// band-edge phase as well as in-band gain, so normalising by it would make
/// "0 dB" approximately rather than derivably true, by an amount no test could
/// state.
///
/// Throws `std::invalid_argument` unless `0 < lowHz < highHz`, the sample rate
/// is positive, and at least one transform bin lands inside the band.
[[nodiscard]] double inBandNormalisation(const Deconvolution& reference,
                                         double lowHz, double highHz);

/// How flat the band actually is, which a caller must not assume.
///
/// The band formula says where the answer is *meaningful*; it does not by
/// itself make the answer flat. Measured, the same band reads +-0.03 dB with a
/// two-octave fade-in and -7.06..+2.40 dB with the fade widths this repo shipped
/// before 2026-08-30. Any claim about absolute level has to quote the figure for
/// the configuration in use, so the figure is available.
[[nodiscard]] BandFlatness bandFlatness(const Deconvolution& reference,
                                        double lowHz, double highHz);

}  // namespace rta::ir
