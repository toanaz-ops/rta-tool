// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// See docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.3, §3.4.
#pragma once

#include "measure/Analyser.h"
#include "measure/Snapshot.h"

#include <cstdint>

namespace rta::measure {

/// What to drive through an `Analyser` to build a deterministic, offline
/// `Snapshot` -- no device, no hardware, no thread. This is the fixture
/// `rtatool_snapshot` uses for `rta-view.png` (plan §1.3) and the one
/// `test_synthetic_snapshot.cpp` checks for bit-identical reproducibility.
struct SyntheticSpec {
    enum class Source { PinkNoise, Sine };

    Source source = Source::PinkNoise;
    double sineHz = 1000.0;
    /// Linear peak amplitude, sine-referenced: 1.0 is a full-scale sine
    /// (0.0 dBFS per Levels.h), converted for either generator via the same
    /// `levelDbFs(amplitude*amplitude*0.5)` this app uses everywhere else
    /// -- one dB convention, not a second one invented for this fixture.
    double amplitude = 0.1;
    /// Only used when `source == PinkNoise`; ignored, but not required to
    /// be anything in particular, otherwise.
    std::uint32_t seed = 0x5EEDu;
    double seconds = 4.0;
    Analyser::Config analysis{};
};

/// Drives `spec.source` through a fresh `Analyser` in fixed-size chunks and
/// returns the one `Snapshot` published at the end. Bit-identical for a
/// given spec (SyntheticPink and SyntheticSine are both exact-by-
/// construction and seeded deterministically; see rta/gen/Synthetic.h) --
/// that is the precondition for `rta-view.png` being reviewable as a diff.
[[nodiscard]] SnapshotPtr makeSyntheticSnapshot(const SyntheticSpec& spec);

}  // namespace rta::measure
