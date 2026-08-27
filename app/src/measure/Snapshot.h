// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// See docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.3, §3.4.
#pragma once

#include "measure/Levels.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace rta::measure {

/// One fractional-octave band's reading, already in the app's own units:
/// hertz and dBFS, never a raw bin index or a linear power. A view draws
/// this directly; it does not need to know anything about FFTs.
struct BandReading {
    float centreHz = 0.0f;
    float lowerHz = 0.0f;
    float upperHz = 0.0f;
    float levelDb = static_cast<float>(kLevelFloorDb);
    /// True when the band spans fewer transform bins than
    /// `BandWeights::kMinBinsPerBand` -- the FFT physically cannot resolve
    /// it at the configured size. See plan §3.5.1 for how the view must
    /// draw this (a boundary line, not just a tint).
    bool underResolved = false;
};

/// One immutable measurement, published by the analysis thread and read by
/// the message thread through an atomic pointer swap (decision record: "one
/// struct, published by atomic pointer swap"; trap T-5).
///
/// Deliberately carries NO timestamp: two snapshots built from the same
/// input must compare equal field-for-field, which is what makes
/// `makeSyntheticSnapshot` deterministic and `rta-view.png` reviewable as a
/// byte-for-byte diff (plan §1.4, §5.6).
struct Snapshot {
    /// Increments by exactly one on every `Analyser::publish`. The only
    /// field a reader needs to notice "there is something new" without
    /// comparing pointers.
    std::uint64_t sequence = 0;

    double sampleRate = 0.0;
    std::size_t fftSize = 0;
    int fraction = 3;

    std::vector<BandReading> bands;

    /// The raw per-bin spectrum, dBFS, `fftSize/2 + 1` long -- the trace a
    /// spectrum overlay reads, as opposed to the banded `bands` a bar chart
    /// reads. Optional in the sense that a caller uninterested in it can
    /// ignore the vector; it is always populated by `Analyser::publish`.
    std::vector<float> spectrumDb;

    bool hasReference = false;
    std::vector<BandReading> referenceBands;

    std::uint64_t framesAnalysed = 0;
    /// Carried straight from `Analyser::publish`'s argument -- the audio
    /// callback's drop count as of this snapshot, so a drop the analysis
    /// thread never asked about still reaches the UI (plan §5.5).
    std::uint64_t droppedSamples = 0;

    float peakBandLevelDb = static_cast<float>(kLevelFloorDb);
    float peakBandCentreHz = 0.0f;
};

/// Readers only ever see a `const Snapshot`: nothing downstream of
/// `Analyser::publish` is allowed to mutate a snapshot in place, which is
/// what makes handing the same pointer to two frames of drawing safe with
/// no lock.
using SnapshotPtr = std::shared_ptr<const Snapshot>;

}  // namespace rta::measure
