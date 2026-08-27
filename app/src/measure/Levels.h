// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// this file is compiled directly into rtatool_analysis_tests, which builds
// on CI even when RTA_BUILD_APP is OFF. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.3, §1.4, §3.4.
#pragma once

#include <cmath>

namespace rta::measure {

/// The floor every level readout clamps to. `log10(0)` on a silent channel
/// would otherwise poison the whole plot with -inf / NaN (plan §1.4).
inline constexpr double kLevelFloorDb = -120.0;

/// `10*log10(0.5)`, the mean-square power of a unit-amplitude sine. Adding
/// this to a plain power-to-dB conversion is what makes a full-scale SINE
/// read exactly 0.0 dBFS -- the reference this whole app reads levels
/// against (plan §1.4), matching every analyser on a rack rather than the
/// "full-scale square wave = 0 dB" convention some measurement software
/// uses instead. `test_synthetic.cpp` in core/tests pins the same constant.
inline constexpr double kFullScaleSineOffsetDb = 3.0102999566398120;

/// Clamp a dB value to the project's floor. Never returns NaN or -inf: a
/// non-finite input (NaN from a 0/0 upstream, or +inf) clamps to the floor
/// too, since neither is a level anything downstream can draw.
[[nodiscard]] inline double clampLevelDb(double db) noexcept {
    if (!std::isfinite(db)) return kLevelFloorDb;
    return db < kLevelFloorDb ? kLevelFloorDb : db;
}

/// Mean-square power -> dBFS, sine-referenced and floor-clamped.
///
/// `power` is a mean-square quantity: `SpectrumEngine::spectrum()[bin]`, a
/// band power out of `BandWeights::apply`, or `amplitude^2` for a known
/// sine. A unit-amplitude sine has mean square 0.5, so
/// `10*log10(0.5) + kFullScaleSineOffsetDb == 0.0` -- that identity is what
/// `test_levels.cpp` checks, and it is the ONE definition of "dB" the rest
/// of the app (Analyser, SyntheticSnapshot, the plot) reads through.
///
/// `power <= 0` (silence, or a small negative value from float round-off)
/// clamps to the floor rather than reaching `std::log10(0) == -inf`; NaN
/// clamps too, since `power > 0.0` is false for NaN under IEEE comparison.
[[nodiscard]] inline double levelDbFs(double power) noexcept {
    if (!(power > 0.0)) return kLevelFloorDb;
    return clampLevelDb(10.0 * std::log10(power) + kFullScaleSineOffsetDb);
}

}  // namespace rta::measure
