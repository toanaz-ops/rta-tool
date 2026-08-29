// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <cstddef>
#include <span>

namespace rta::dsp {

/// Result of a GCC-PHAT search.
struct DelayEstimate {
    /// Integer peak lag, in samples. POSITIVE means the measurement lags the
    /// reference -- the same sign `DualFftEngine::Config::referenceDelaySamples`
    /// uses, so this value feeds that field directly with no negation anywhere
    /// in between. A flipped sign here would make the compensation add to the
    /// error it is meant to remove.
    std::ptrdiff_t delaySamples = 0;

    /// Parabolic refinement around the integer peak, in samples, |.| <= 0.5.
    /// Without this the finder cannot see anything finer than one sample --
    /// 1/48000 s is 7.1 mm of air, and Friture's own source concedes that
    /// decimation without sub-sample interpolation caps delay resolution at
    /// roughly 3 cm.
    ///
    /// KNOWN GAP, not covered by this project's test suite: the fit signs the
    /// whole three-sample triple by the centre sample before fitting (see the
    /// implementation), which is correct because the fit describes one
    /// continuous peak, not three independent magnitudes. But the classic
    /// parabola ratio `(r[-1]-r[+1]) / (r[-1]-2*r[0]+r[+1])` is invariant
    /// under negating all three samples by the same factor, so this signed
    /// treatment agrees EXACTLY with the naive "take |.| of each sample
    /// independently" alternative whenever the triple already shares one
    /// sign -- which is every case a smooth PHAT peak produces, and so every
    /// case this test suite exercises. The two treatments diverge only when a
    /// neighbour sample has the opposite sign from the centre (a true
    /// fractional delay near a zero crossing of the correlation), and even
    /// then only by a fraction of a sample. No fixture here provokes that
    /// condition, so this file's tests do not distinguish the correct
    /// treatment from the wrong one. Treat it as unverified, not proven.
    double subSample = 0.0;

    /// Normalised |correlation| at the peak, ~0..1. A perfect, fully
    /// populated match reads close to 1.0; noise or a narrow analysis band
    /// erodes it towards 0 without moving the peak -- this is what lets an
    /// operator see that a delay reading is untrustworthy.
    double peak = 0.0;

    /// The peak was negative: the two channels agree on timing but disagree on
    /// polarity (a swapped-pin cable, a flipped driver). Reported here rather
    /// than folded into the phase, because a polarity flip and a half-cycle
    /// delay must never look the same to whoever reads this struct.
    bool inverted = false;
};

/// Options for `findDelayPhat`.
struct PhatOptions {
    double sampleRate = 48000.0;

    /// Floor for the PHAT weighting, RELATIVE to max|G| across the analysed
    /// band. An absolute floor -- Open Sound Meter uses -140 dBFS -- couples
    /// the estimator to input level: the same signal 20 dB quieter would be
    /// regularised differently and could shift the answer. A floor relative to
    /// the loudest bin in THIS pair tracks whatever level the two channels
    /// happen to be at, which is what makes the estimate level-independent.
    /// Friture's 1e-10 is the value adopted here; see the decision record §4.
    double regularisation = 1e-10;

    /// Band limit for the correlation, in hertz. 0 = DC / Nyquist (no limit).
    /// PHAT's flat weighting is what makes it robust in reverberation, but the
    /// same flattening amplifies bins that carry no real signal in a low-SNR
    /// narrowband recording. A band limit is PHAT's one mitigation for that
    /// case -- it is not a second weighting scheme, just fewer bins handed to
    /// the same one.
    double minHz = 0.0;
    double maxHz = 0.0;
};

/// Generalised cross-correlation with phase transform: for cross-spectrum
/// `G[k] = conj(X[k]) * Y[k]` (the same `Sxy` convention used everywhere else
/// in this codebase, X = reference, Y = measurement), PHAT weights by
/// `psi[k] = 1 / max(|G[k]|, regularisation * max|G|)` before inverting back
/// to the time domain and searching for the largest peak.
///
/// PHAT is flat-weighted, which is what makes it robust in reverberation --
/// the case this tool exists for. Roth weighting (`1/Gxx`, which is what a
/// plain A/B deconvolution computes, as Open Sound Meter does) biases the
/// estimate toward frequencies where the reference happens to be strong; on a
/// clean electrical loopback the two agree, so the choice only shows up in the
/// situation that matters. See `docs/dsp/2026-08-28-dual-fft.md` §4.
///
/// Both spans are zero-padded to a linear (non-circular) correlation length
/// before the transform, so a real delay never gets contaminated by the tail
/// of the block wrapping around onto its own head.
///
/// @throws std::invalid_argument on an empty span, mismatched lengths, or
///         `maxHz` positive and below `minHz`.
[[nodiscard]] DelayEstimate findDelayPhat(std::span<const float> reference,
                                           std::span<const float> measurement,
                                           const PhatOptions& options);

}  // namespace rta::dsp
