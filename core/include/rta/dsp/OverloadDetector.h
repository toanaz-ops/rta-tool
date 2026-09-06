// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <cstddef>
#include <span>

namespace rta::dsp {

/// Smaart LE v9.1 p.78's own published criterion (record §8.1): three or
/// more consecutive samples with |x| >= this. The threshold is the coarsest
/// integer full scale a converter can deliver -- int16 clips at
/// 1 - 2^-15 = 0.999969482421875, int24 at 1 - 2^-23, int32 at 1 - 2^-31
/// (rounds to exactly 1.0f in float32), float at 1.0 -- all AT OR ABOVE this
/// value, so it is a statement in the vocabulary of the thing measured, not a
/// grid floor read off a slider. Both 1-2^-15 and 1-2^-23 are exactly
/// representable in float32 (15 and 23 mantissa bits against 24 available),
/// so the comparison below is exact; verified 2026-09-06.
inline constexpr float kFullScaleThreshold = 1.0f - 0x1p-15f;

/// True iff `x` contains `runLength` or more CONSECUTIVE samples with
/// `|x| >= threshold`. Pure, no allocation, no state -- run on the analysis
/// thread over each drained hop, never in the audio callback (CLAUDE.md's
/// real-time rule). A run does not carry across separate calls: each call
/// sees only the span it is given, so a hop boundary can split a run in two
/// (record §8's capture-window latch is what stitches runs across hops, one
/// layer up in app/).
[[nodiscard]] bool hasOverload(std::span<const float> x, int runLength = 3,
                               float threshold = kFullScaleThreshold) noexcept;

}  // namespace rta::dsp
