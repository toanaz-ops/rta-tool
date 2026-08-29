// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. This file MUST NOT depend on JUCE, Qt, or any
// audio-device API. See docs/specs for the rationale.
#pragma once

#include <complex>
#include <cstddef>
#include <span>

namespace rta::dsp {

/// Group delay from the closed form, evaluated with a central finite
/// difference over the complex transfer function:
///
///     tau_g(w) = -Im( dH/dw * conj(H) ) / |H|^2
///
/// This lives in core, not in the view (docs/dsp/2026-08-28-dual-fft.md §6),
/// because it is a DERIVATIVE: it amplifies bin-to-bin noise, so it needs the
/// complex H and a stated smoothing width, not a difference of whatever
/// numbers the screen happens to be showing. Phase unwrap is the opposite
/// case -- ambiguous and history-dependent -- and stays in the view for
/// exactly that reason; the two do not belong in the same layer.
///
/// `smoothingBins` sets the half-width of the central difference: bin `k`
/// compares `h[k+m]` against `h[k-m]`. A wider window is a smoother, more
/// biased estimate of the SAME true delay -- there is no width that is
/// "correct" independent of how noisy the underlying H is, so the caller
/// must state the width alongside every number this produces.
///
/// @param h              numBins values, DC to Nyquist.
/// @param binWidthHz     sampleRate / fftSize. Must be positive.
/// @param smoothingBins  half-width of the central difference, >= 1.
/// @param out            numBins seconds, same length as `h`. Edge bins (and
///                       any bin where the full central window would reach
///                       past DC or Nyquist) fall back to the widest one-sided
///                       difference that fits -- the honest choice, rather
///                       than inventing bins beyond the spectrum's ends.
/// @throws std::invalid_argument if `smoothingBins == 0`, `out.size() !=
///         h.size()`, or `binWidthHz <= 0`.
///
/// A bin where `|H| == 0` makes the closed form 0/0; this returns 0.0 for
/// that bin rather than NaN. One NaN in a trace propagates through every
/// min/max the view computes over it, so a silent zero is the safer failure.
void groupDelaySeconds(std::span<const std::complex<double>> h,
                       double binWidthHz,
                       std::size_t smoothingBins,
                       std::span<double> out);

}  // namespace rta::dsp
