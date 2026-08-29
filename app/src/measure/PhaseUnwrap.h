// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// See docs/plans/2026-08-29-L2-dual-fft-impl-plan.md, Task 6.
#pragma once

#include <span>

namespace rta::measure {

/// Controls how `unwrapPhase` treats bins whose coherence is too low to
/// trust their own reading.
struct UnwrapOptions {
    /// Bins at or below this coherence are skipped when deciding whether
    /// the next trusted bin needs a 2*pi correction, instead of being
    /// trusted to carry that step themselves -- see the coherence-gated
    /// overload below. This RESUMES from the last trusted bin (the running
    /// offset is held, not reset), it does not restart the unwrap from
    /// zero; a bad bin costs one bad-looking frame, not the accumulated
    /// history before it. 0 disables the gate: every bin is trusted,
    /// whatever its coherence.
    float minimumCoherence = 0.0f;
};

/// Classic 1-D unwrap: add or subtract 2*pi whenever the step between
/// neighbours exceeds pi.
///
/// This is a DISPLAY operation and never feeds back into the engine. It is
/// history-dependent, so it is recomputed from the wrapped trace every
/// frame -- never accumulated across frames.
///
/// Throws std::invalid_argument if `out.size() != wrapped.size()`.
void unwrapPhase(std::span<const float> wrapped, std::span<float> out,
                 const UnwrapOptions& options = {});

/// Coherence-gated overload. A bin at or below `options.minimumCoherence`
/// contributes nothing to the running unwrap: the step INTO the next good
/// bin is measured against the last TRUSTED bin, not the bad one, so one
/// ambiguous reading costs one bad frame instead of a 2*pi step that every
/// bin above it would otherwise inherit for good.
///
/// Bin 0 gets no exemption from the gate, even though it seeds everything
/// else: an untrusted bin 0 anchoring the whole trace is the same failure
/// as any other bad bin poisoning everything above it, and DC is exactly
/// where coherence is worst in practice (nothing to correlate below the
/// measurement's low-frequency limit) -- so this is the common case to
/// protect against, not a corner one. Until the first trusted bin is seen,
/// every bin is passed through as its own raw value.
///
/// `coherence` must be the same length as `wrapped`. Throws
/// std::invalid_argument otherwise, or if `out.size() != wrapped.size()`.
///
/// Two inputs this function does not validate, because both fall out
/// reasonably rather than needing a rule:
///  - NaN in `wrapped` becomes the anchor for whichever bin holds it, and
///    suppresses the wrap check for the two steps touching it (a NaN delta
///    compares false against both thresholds). One bad frame stays a
///    display glitch at that bin; it does not propagate.
///  - NaN in `coherence` compares false against `minimumCoherence`, so a
///    NaN coherence reading is treated as TRUSTED, not gated. A producer
///    that can emit NaN coherence should floor it before calling this --
///    this function's job is the unwrap, not sanitising its inputs.
void unwrapPhase(std::span<const float> wrapped, std::span<const float> coherence,
                 std::span<float> out, const UnwrapOptions& options);

}  // namespace rta::measure
