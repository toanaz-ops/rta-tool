// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// enforced by the measure_has_no_framework_deps ctest.
//
// L7-EQ UI task T2 (docs/plans/2026-09-29-eq-ui-lane-plan.md): the pure
// functions that turn a stored Trace's arrays into what EqSession::
// setMeasurement takes. Split out of EqPaneModel so the model stays about
// state and the conversions stay checkable one at a time.
#pragma once

#include "trace/Trace.h"

#include <complex>
#include <span>
#include <vector>

namespace rta::measure {

/// hz_k = k * binHz(), the double product narrowed once, so bin k of a 64-point
/// 48 kHz trace is bitwise the float k * 750. Bin 0 is DC.
[[nodiscard]] std::vector<float> traceBinFrequencies(const rta::trace::Trace& trace);

/// H_k = 10^(m_k/20) * exp(i*phi_k). `phaseRad` is in RADIANS, as a Trace
/// stores it (CaptureConverter's degToRadPhase) -- reading it as degrees is
/// the defect the test's m = 20*log10(0.5), phi = pi/2 fixture exists for.
[[nodiscard]] std::vector<std::complex<double>> traceComplexResponse(std::span<const float> magnitudeDb,
                                                                     std::span<const float> phaseRad);

/// A stored array must be finite for the allocator's log/exp arithmetic to mean
/// anything; a caller refuses rather than feed NaN into a fit. Empty is finite.
[[nodiscard]] bool allFinite(std::span<const float> values);

/// A target curve onto `dstHz`. Empty `srcHz` is FLAT (0 dB everywhere). Equal
/// grids copy bitwise (a resample of an identical grid is the identity and must
/// not round). Otherwise linear in log10(f) and linear in dB -- FirDesign.h's
/// interpolation rule, exact for a straight tilt -- built from the source's
/// positive-frequency bins (log10(0) has no place in that rule) and clamped to
/// the end values outside them.
[[nodiscard]] std::vector<float> resampleTargetDb(std::span<const float> srcHz, std::span<const float> srcDb,
                                                  std::span<const float> dstHz);

}  // namespace rta::measure
