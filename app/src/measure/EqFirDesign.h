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
/// M/2, sampleRate <= 0). Even and odd `taps` are both exact: core's
/// linear-phase design samples an even-length filter on the half-sample grid
/// (FIR record Sec.4, amendment 2026-09-29), so a flat target reads |H| = 1 at
/// DC for N = 1024 and 4096 alike. (Until that fix an even `taps` doubled the
/// DC gain -- flat 2.0, the +6 dB peaking fixture 2.04 -- and this function
/// refused it; the picker offered N-1 instead.)
[[nodiscard]] rta::dsp::FirResult designEqFir(std::span<const CommittedFilter> filters,
                                              double sampleRate, std::size_t taps,
                                              rta::dsp::FirPhase phase);

}  // namespace rta::measure
