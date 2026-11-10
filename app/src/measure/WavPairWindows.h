// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE. Internal to WavPairClock.cpp.
//
// H12 T3 (record E3/E6): K windows spread evenly over a range; in each, an
// integer GCC-PHAT lag then a phase-slope fraction -> one delay observation.
#pragma once

#include "measure/WavTransfer.h"
#include "rta/dsp/ClockDrift.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace rta::measure::detail {

struct WindowPass {
    std::int64_t windowLength = 0;
    int windowCount = 0;
    std::int64_t priorDelay = 0;  ///< delay the integer search is centred on, samples
    std::int64_t lagBound = 0;    ///< integer lag searched within +- this
    double sampleRate = 0.0;
    /// Band (Hz) of the integer GCC-PHAT and of the phase-slope fit; maxHz 0 = the
    /// full band (PHAT: Nyquist, fit: fs/4). See `driftBand`.
    double minHz = 0.0;
    double maxHz = 0.0;
    /// The measurement samples [measFirst, measLast] a window may touch (the
    /// meas file for pass 1; the pair's valid range for pass 2). A window that
    /// does not fit is SKIPPED, never read: a read outside must not become ReadFailed.
    std::int64_t measFirst = 0;
    std::int64_t measLast = -1;
};

struct WindowTally {
    int accepted = 0;
    int skipped = 0;
    double bestTrust = 0.0;   ///< highest delay trust over every window tried
    double lowestAcceptedTrust = 1.0;  ///< lowest trust among Accepted windows (1 when none)
    double trustFloor = 0.0;  ///< the trust it needed (acceptMultiple * null floor)
};

/// The band the clock search works in before the drift is removed. Drift smears
/// each window by eps * N samples, and a delay smear of s samples decorrelates a
/// bin at f by about (2 pi f s / fs)^2 -- the HIGH bins go first, and PHAT weights
/// every bin equally, so they drag the peak down with them. Restricting to
/// 100 Hz .. min(fs/8, 3 kHz) keeps the bins that survive a large smear (s = 5
/// samples at 3 kHz is 0.2 rad). Pass 2, on the corrected pair, uses the full band.
struct DriftBand {
    double minHz;
    double maxHz;
};
[[nodiscard]] inline DriftBand driftBand(double sampleRate) noexcept {
    return {100.0, std::min(sampleRate / 8.0, 3000.0)};
}

enum class WindowStatus { Ok, Cancelled, ReadFailed };

/// Appends one `DelayObservation` per Accepted window whose fraction resolved.
/// positionSamples is the window centre relative to `rangeStart`, so a line fit
/// has its intercept at the range start.
[[nodiscard]] WindowStatus measureWindows(const SampleBlockSource& reference, int referenceChannel,
                                          const SampleBlockSource& measurement, int measurementChannel,
                                          std::int64_t rangeStart, std::int64_t rangeLength,
                                          const WindowPass& pass, const std::atomic<bool>& cancel,
                                          std::vector<rta::dsp::DelayObservation>& observations,
                                          WindowTally& tally);

}  // namespace rta::measure::detail
