// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. This file MUST NOT depend on JUCE, Qt, or any
// audio-device API. See docs/specs for the rationale.
#pragma once

#include <cstddef>
#include <span>

namespace rta::dsp {

/// Harris 1978's overlap correlation: how much of one windowed frame survives
/// in the next one, `lag` samples later.
///
///     c(m) = sum_n w[n] * w[n+m]  /  sum_n w[n]^2
///
/// The numerator runs ONLY over samples where both frames exist -- it does not
/// wrap around the block. Wrapping turns periodic Hann's exact 1/6 at 50 %
/// overlap into 1/3, and 1/3 is exactly the kind of wrong number that looks
/// plausible.
[[nodiscard]] double overlapCorrelation(std::span<const float> window,
                                        std::size_t lag) noexcept;

/// Effective number of INDEPENDENT averages behind `frames` equally-weighted
/// overlapped frames.
///
///     Neff = K / (1 + 2 * sum_{m=1}^{K-1} (1 - m/K) * c(m*hop)^2)
///
/// Overlapped frames share samples, so K of them buy fewer than K averages'
/// worth of variance reduction. Nothing else in this codebase may report a raw
/// frame count as an average count: the coherence gate is built on this number.
///
/// `hop == 0` means every frame is the SAME samples again -- there is no
/// advance between them -- so however many `frames` there are, they carry
/// exactly one independent average's worth of information. Returns 1.0 rather
/// than looping to find the lag at which `c` finally reaches zero, because at
/// hop 0 it never does.
[[nodiscard]] double fifoEffectiveAverages(std::span<const float> window,
                                           std::size_t hop,
                                           std::size_t frames) noexcept;

/// Effective averages for a one-pole average seeded from its own first frame
/// (the seeding SpectrumEngine already does).
///
/// Weights after K frames are w1 = (1-a)^(K-1), wi = a(1-a)^(K-i); they sum to
/// 1, so Neff_raw = 1 / sum(wi^2), which is exactly 1 at K = 1 and tends to
/// (2-a)/a. The overlap penalty is then applied to the frames BEYOND the first:
///
///     Neff = 1 + (Neff_raw - 1) / D,  D = 1 + 2 * sum_{m>=1} c(m*hop)^2
///
/// so a single frame still counts as exactly one average, and the K -> inf
/// limit agrees with fifoEffectiveAverages.
///
/// `hop == 0` carries the same precondition as fifoEffectiveAverages, for the
/// same reason -- every frame is the same samples again, so it returns 1.0
/// regardless of `alpha` or `frames`. Without this guard the overlap-penalty
/// sum below never reaches a lag where `c` is zero, because at hop 0 it never
/// is, and the loop that looks for one does not terminate.
[[nodiscard]] double exponentialEffectiveAverages(std::span<const float> window,
                                                  std::size_t hop,
                                                  double alpha,
                                                  std::size_t frames) noexcept;

}  // namespace rta::dsp
