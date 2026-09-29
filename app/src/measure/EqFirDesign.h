// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// L7-EQ UI task T3 (docs/plans/2026-09-29-eq-ui-lane-plan.md, decision D11).
// What EXPORT FIR realises: the summed response of the committed filters that
// are NOT yet in the rig, sampled onto the half-grid rta::dsp::designFir's
// per-bin overload takes (FirDesign.h, record Sec.6 amendment D2). The FIR
// record's own rule for that grid -- M a power of two, M >= 8N (record Sec.2)
// -- is FirDesign.cpp's private `nextPowerOfTwo(8 * taps)`; it is restated
// here because the grid has to be built BEFORE the call, and the two are
// pinned together by the test (designFftSize must equal the M used here).
#pragma once

#include "measure/EqSession.h"

#include "rta/dsp/FirDesign.h"

#include <cstddef>
#include <span>
#include <vector>

namespace rta::measure {

/// M = smallest power of two >= 8 * taps. 4096 taps -> 32768.
[[nodiscard]] std::size_t eqFirGridSize(std::size_t taps);

/// Linear magnitude 10^(sum_i R_i(f_k) / 20) at f_k = k * fs / M for
/// k = 0..M/2 (M/2 + 1 bins, DC..Nyquist), summed over every filter with
/// `applied == false`. An applied filter is already in the signal path the
/// measurement came through (CommittedFilter's doc comment), so realising it
/// again would put its correction in the rig twice. No filters -> exactly
/// 1.0f in every bin.
[[nodiscard]] std::vector<float> eqFirMagnitudeHalfGrid(std::span<const CommittedFilter> filters,
                                                        double sampleRate, std::size_t taps);

/// designFir over that grid. Throws what designFir throws (taps < 8, taps >
/// M/2, sampleRate <= 0), and ALSO refuses an EVEN `taps`.
///
/// Why even is refused (found building this lane, 2026-09-29): FirDesign.cpp's
/// linear-phase core puts the zero-phase impulse's centre sample at BOTH
/// central taps of an even-length filter (taps[N/2-1] == taps[N/2] ==
/// h0[0]*w), so h[0] is counted twice. Measured on a flat target: |H(0)| =
/// 2.00000 at N = 4096 (+6.02 dB), against 1.00001 for N = 4095 and 1.00011
/// for N = 1023; a +6 dB peaking filter reads 3.0195 (even) vs 1.9945 (odd).
/// Every magnitude test in core/tests uses odd N (1023, 511, 255), so nothing
/// there sees it. Until
/// FirDesign designs even N on a half-sample grid, an even-N export would be a
/// filter with the wrong gain and a correct-looking header; odd lengths are
/// exact (Type I linear phase, an integer group delay of (N-1)/2).
/// @throws std::invalid_argument on even `taps`.
[[nodiscard]] rta::dsp::FirResult designEqFir(std::span<const CommittedFilter> filters,
                                              double sampleRate, std::size_t taps,
                                              rta::dsp::FirPhase phase);

}  // namespace rta::measure
