// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// See docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.3, §3.4.
#pragma once

#include "measure/Analyser.h"
#include "measure/Snapshot.h"

#include "rta/dsp/MtwLayout.h"

#include <cstddef>
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

/// A deterministic transfer function: a pure delay, a notch, and a coherence
/// dip. Built from the same closed forms the preview mockup uses so the
/// picture stays recognisable, but as PER-BIN ARRAYS rather than a function
/// sampled once per pixel -- which is the fourth mockup accident the L5c
/// record names, and the one that composes with nothing.
///
/// Deterministic for the same reason makeSyntheticSnapshot is: `transfer.png`
/// has to be reviewable as a byte-for-byte diff.
[[nodiscard]] TransferBlock makeSyntheticTransfer(std::size_t fftSize, double sampleRate,
                                                  int delaySamples);

/// The MTW twin of `makeSyntheticTransfer`: the SAME closed-form magnitude and
/// coherence curves (`magnitudeDbAt` / `coherenceAt`), evaluated at the MTW
/// engine's own explicit, per-band frequency vector (`rta::dsp::mtwFrequencies`)
/// instead of a fixed bin width -- so a picture built from this fixture stays
/// recognisable next to `transfer.png`'s fixed curve. Every band descriptor
/// (`seamHz`, `firstIndex`, `windowSeconds`, `integrationSeconds`) comes
/// straight from `rta::dsp::mtwBands`, never a hand-picked number, which is
/// what puts the seams at the record's own frequencies (187.5/375/750/1500/
/// 3000/6000 Hz at the default `config`) rather than wherever this fixture
/// happened to draw a line.
///
/// `effectiveAverages` is fixed at the record's own closed-form constant for
/// a FULLY FILLED FIFO at the default depth (docs/dsp/2026-09-05-mtw-l3.md
/// §5; `test_analyser_mtw.cpp` checks the same digits against the real
/// engine) -- this fixture states a completed measurement, not a mid-fill
/// one, so `coherenceAvailable` is true in every band and this is never
/// recomputed from a window here.
[[nodiscard]] MtwBlock makeSyntheticMtw(const rta::dsp::MtwConfig& config = rta::dsp::MtwConfig{},
                                        int delaySamples = 0);

}  // namespace rta::measure
