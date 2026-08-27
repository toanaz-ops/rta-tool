// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/dev/preview. See
// docs/specs/2026-08-28-interactive-tuning-visuals.md.
#pragma once

#include <algorithm>
#include <cmath>

/// Pure analytic-curve math shared by the three lane-L5 preview components
/// (TransferFunctionPreview, TargetMatchPreview, PhaseAlignPreview). Header
/// only, no JUCE: the point of building the synthetic curves from these
/// pieces instead of `rand()` is that a reader can look at one call --
/// `logGaussianDipDb(hz, 2000.0, 0.35, 8.0)` -- and know exactly what shape
/// it draws, which a noise table never lets you do (project CLAUDE.md
/// "Before each phase", applied here to a mockup instead of a DSP feature:
/// the picture still has to be defensible).
namespace rta::dev::preview {

/// `<cmath>` does not portably define pi (MSVC needs `_USE_MATH_DEFINES`
/// before every include, which is a header-order trap), so the one place
/// that needs it names its own constant instead.
inline constexpr double kPi = 3.14159265358979323846;

/// Hermite smoothstep: 0 below `lo`, 1 above `hi`, an S-curve between. The
/// one ramp shape every curve in these previews is built from -- a shelf
/// "turns on" over `[lo, hi]` instead of snapping, which is what keeps a
/// hand-written curve from looking hand-written.
[[nodiscard]] inline double smoothstep(double x, double lo, double hi) noexcept {
    if (lo == hi) return x < lo ? 0.0 : 1.0;
    const double t = std::clamp((x - lo) / (hi - lo), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

/// A dip (or, with a negative `depthDb`, a peak) shaped as a gaussian in LOG
/// frequency -- `widthOctaves` is a natural unit for a filter's Q the way a
/// width in Hz never is, because a notch's shape looks the same at 250 Hz
/// and 2 kHz when measured in octaves either side of centre.
[[nodiscard]] inline double logGaussianDb(double hz, double centreHz, double widthOctaves,
                                           double depthDb) noexcept {
    const double x = std::log2(hz / centreHz) / widthOctaves;
    return -depthDb * std::exp(-0.5 * x * x);
}

/// An S-shaped phase swing of amplitude `amplitudeDeg`, centred on
/// `centreHz`, `widthOctaves` wide -- the phase behaviour a minimum-phase
/// resonance actually has (arctan rather than a straight ramp, so the swing
/// eases in and out instead of kinking).
[[nodiscard]] inline double logArctanSwingDeg(double hz, double centreHz, double widthOctaves,
                                               double amplitudeDeg) noexcept {
    const double x = std::log2(hz / centreHz) / widthOctaves;
    return amplitudeDeg * (2.0 / kPi) * std::atan(x);
}

/// Wrap an angle in degrees to `(-180, 180]` -- every phase trace in these
/// previews is drawn wrapped (spec: "phase trace beneath (wrapped +-180)"),
/// so a curve built from a monotonic formula still has to fold back through
/// this before it is plotted.
[[nodiscard]] inline double wrapDegrees180(double deg) noexcept {
    double wrapped = std::fmod(deg + 180.0, 360.0);
    if (wrapped < 0.0) wrapped += 360.0;
    return wrapped - 180.0;
}

}  // namespace rta::dev::preview
