// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/dsp/DualFftEngine.h"
#include "rta/dsp/Window.h"

#include <cstddef>
#include <vector>

namespace rta::dsp {

/// The multi-time-window band table: which FFT size and which bins each band
/// of the MTW engine owns. Pure arithmetic -- no FFT, no state, no signal --
/// so every number here is checkable with a calculator against
/// docs/dsp/2026-09-05-mtw-l3.md §2-§3 and the plan's corrected §3 table
/// (conflicts C1-C3).
///
/// ORDERING WARNING, the single most likely off-by-one in this file: the
/// record's own band index `k` runs 0 (top, smallest FFT) to `octaveCount`
/// (bottom, largest FFT). Every function below returns/consumes bands in the
/// OPPOSITE order -- ascending in FREQUENCY, i.e. vector index 0 is the
/// bottom band (`k == octaveCount`) and the last index is the top band
/// (`k == 0`). `k = octaveCount - vectorIndex`. Every number in the decision
/// record's §3 table and this plan's corrected table is written in vector
/// (ascending-frequency) order for exactly this reason.
struct MtwConfig {
    std::size_t topFftSize = 1024;   ///< N0, restricted to {512, 1024, 2048}
    std::size_t octaveCount = 6;     ///< K; bands are k = 0..K, so K+1 of them
    double sampleRate = 48000.0;
    WindowType window = WindowType::Hann;

    /// Record §5 (reversed from an earlier seconds-based draft): averaging is
    /// specified in FRAMES, uniform across bands, and SECONDS are reported
    /// per band (MtwBand::integrationSeconds below). Neither knob has a
    /// seconds twin, because uniform seconds cannot give uniform confidence:
    /// Neff per second of integration scales as 1/T_window, and a table whose
    /// windows span 64x (1024..65536 samples) would then span 64x in Neff for
    /// one shared tau (conflict C3's two evidence tables). There is no
    /// per-band tau in this Config on purpose -- it is the thing that must
    /// stay impossible to express.
    TransferAveraging averaging = TransferAveraging::Fifo;
    std::size_t fifoDepth = 16;         ///< frames, 1..32. Neff = 8.5866271 at 16.
    double timeConstantFrames = 16.0;   ///< frames, > 0. alpha = 1 - exp(-1/this).

    int referenceDelaySamples = 0;
    double minimumEffectiveAverages = 8.0;
};

/// One band's layout: which FFT size it runs, which bins it owns, and where
/// those bins land in the stitched, ascending-frequency output vector.
struct MtwBand {
    std::size_t fftSize = 0;
    std::size_t hopSize = 0;        ///< fftSize / 4
    std::size_t firstBin = 0;       ///< inclusive
    std::size_t lastBin = 0;        ///< inclusive
    std::size_t firstIndex = 0;     ///< where this band starts in the stitched vector
    double lowerEdgeHz = 0.0;       ///< fs / (8 * 2^k); 0.0 for the bottom band
    double windowSeconds = 0.0;         ///< fftSize / fs
    double integrationSeconds = 0.0;    ///< fifoDepth * hopSize / fs -- what the view prints
};

/// Ascending in frequency: index 0 is the bottom (largest-FFT) band.
[[nodiscard]] std::vector<MtwBand> mtwBands(const MtwConfig& config);

/// Closed form (record §3, conflict C2, corrected to 1281 for the defaults):
///   (N0/2 - N0/8 + 1)   -- top band, DC..Nyquist inclusive of Nyquist
/// + (K - 1) * (N0/8)    -- the K-1 middle bands, each N0/8 bins wide
/// + (N0/4)              -- bottom band, its owned range starts at DC
[[nodiscard]] std::size_t mtwPointCount(const MtwConfig& config);

/// The stitched frequency vector, strictly increasing, `mtwPointCount(config)`
/// entries long. Point `i` of band `k` is `bin * fs / N_k` for that band's own
/// `N_k` -- never `N_0`, which is why a per-band bin frequency and not a
/// shared bin width is the load-bearing quantity here.
[[nodiscard]] std::vector<double> mtwFrequencies(const MtwConfig& config);

/// 1 - exp(-1/timeConstantFrames). Takes a band index and IGNORES it: the
/// value is band-independent BY CONSTRUCTION, not by coincidence. Every band's
/// DualFftEngine gets `timeConstantSeconds_k = timeConstantFrames * hop_k/fs`,
/// so that engine's own `alpha_k = 1 - exp(-(hop_k/fs)/timeConstantSeconds_k)`
/// collapses the `hop_k/fs` back out:
///
///     alpha_k = 1 - exp(-1/timeConstantFrames)      for every k
///
/// The parameter is kept anyway so a caller cannot silently start assuming
/// band-dependence; task 1 case 8 asserts the equality bit-for-bit.
[[nodiscard]] double mtwAlpha(const MtwConfig& config, std::size_t band) noexcept;

/// `fifoDepth * hop_k / fs` for the given band (ascending-frequency index).
/// This is the ONE place seconds are derived in this header, and they are
/// derived FROM frames, never specified independently -- see MtwConfig's
/// averaging comment.
[[nodiscard]] double mtwIntegrationSeconds(const MtwConfig& config, std::size_t band);

/// Throws std::invalid_argument if the table cannot be built or exceeds the
/// bounds the record sets: topFftSize outside {512,1024,2048}; octaveCount 0
/// or > 10 (N0*2^K must stay a transform this machine will allocate -- at
/// N0=2048, K=10 the bottom band is 2Mi points, 32 MB for one ring);
/// non-positive sampleRate or timeConstantFrames; minimumEffectiveAverages
/// below 1.0 (DualFftEngine::validate enforces the same floor); fifoDepth 0 or
/// above the memory cap of 32 (record §2's memory section: 32 frames is
/// 66.6 MB of FIFO across the seven default bands, and depth 16 already
/// clears the coherence gate in every band -- 33 buys nothing but 2.08 MB).
void validate(const MtwConfig& config);

}  // namespace rta::dsp
