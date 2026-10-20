// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// enforced by the measure_has_no_framework_deps ctest.
//
// How long a live TRANSFER measurement must be fed one excitation before a
// published snapshot can be trusted to contain it. Moved out of
// EqVerifyRunner (L7-EQ UI wave B) when ALIGN-UI's AlignRunner needed the same
// number per solo step (docs/plans/2026-10-20-align-ui-lane-plan.md, T1): two
// copies of one physical fact would be two things to keep correct.
#pragma once

#include "measure/Analyser.h"

#include <cstddef>

namespace rta::measure {

/// depth * fftSize / fs, in seconds. Analyser.h: the transfer FIFO holds
/// `transferFifoDepth` frames of one window each, so depth * fftSize samples
/// of pure excitation guarantee no pre-excitation frame is left in it
/// (deliberately the generous reading: frames overlap, so the true flush is
/// shorter).
[[nodiscard]] inline double transferDwellSeconds(const CaptureConfig& config, std::size_t fftSize,
                                                 double sampleRate) {
    return static_cast<double>(config.transferFifoDepth) * static_cast<double>(fftSize) /
           sampleRate;
}

}  // namespace rta::measure
