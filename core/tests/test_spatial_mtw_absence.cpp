// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Station-5 MUST-FIX: spatialAverageMtw() must carry a band's REAL per-bin
// SpatialBinState even when the whole band is absent, never a placeholder
// that discards it. Positions are hand-built TransferSnapshots (test files
// sit outside check_coherence_gate.cmake's glob -- its own header note), so
// no MtwEngine needs to run for either case: only the combine's own logic is
// under test here, exactly as test_spatial_average.cpp tests spatialAverage()
// without a DualFftEngine in the path.
//
// docs/dsp/2026-09-06-multichannel-l6b.md §3: two distinct absences are
// defined because the weights can legitimately sum to zero where
// contributors exist. Before this fix, spatialAverageMtw() substituted a
// same-shaped "emptyBandResult" for any band whose own spatialAverage() call
// returned nullopt (every bin's weight summed to zero), and that placeholder
// reported EVERY bin as {NoContributor, contributors=0} regardless of what
// the real per-bin state was -- a bin with two gate-passed contributors and
// zero weight (NoWeight) became indistinguishable from a bin nobody
// measured. See memory/a-fixed-defect-returns-through-the-silent-fallback.md.

#include "rta/dsp/SpatialMtw.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <vector>

using namespace rta::dsp;

namespace {

/// A hand-built, gate-passed per-band TransferSnapshot: uniform |H|=1, phase
/// 0, and coherence `gamma2` at every bin. `bins` must match the real
/// MtwEngine bin count for that band (fftSize/2 + 1) so the fixture agrees
/// with mtwBands()'s own layout -- callers pass `band.fftSize / 2 + 1`.
TransferSnapshot bandFixture(std::size_t bins, float gamma2) {
    TransferSnapshot snap;
    snap.sampleRate = 48000.0;
    snap.binWidthHz = 46.875;
    snap.h.assign(bins, std::complex<double>(1.0, 0.0));
    snap.magnitudeDb.assign(bins, 0.0f);
    snap.phaseRadians.assign(bins, 0.0f);
    snap.coherence = std::vector<float>(bins, gamma2);
    return snap;
}

/// Two identical positions' full MtwResult, gamma2 = `bandGamma2[b]` at every
/// bin of band `b`. Only `config`, `bandSnapshots` and `frequencyHz` are
/// touched by spatialAverageMtw(), so the stitched h/magnitudeDb/phaseRadians
/// arrays are left default-sized but unused by the function under test.
MtwResult makePosition(const MtwConfig& cfg, const std::vector<MtwBand>& bandLayout,
                        std::span<const float> bandGamma2) {
    MtwResult result;
    result.config = cfg;
    result.bands = bandLayout;
    result.frequencyHz = mtwFrequencies(cfg);
    result.bandSnapshots.reserve(bandLayout.size());
    for (std::size_t b = 0; b < bandLayout.size(); ++b) {
        result.bandSnapshots.push_back(bandFixture(bandLayout[b].fftSize / 2 + 1, bandGamma2[b]));
    }
    return result;
}

}  // namespace

TEST_CASE("a band that is entirely NoWeight keeps its real per-bin state, not a placeholder",
          "[spatial][mtw][absence]") {
    const MtwConfig cfg;  // defaults: 1024/6 -> 7 bands
    const auto bandLayout = mtwBands(cfg);
    REQUIRE(bandLayout.size() == 7);

    // Every band gamma2 = 0.9 (present, contributors=2) EXCEPT band 3, which
    // is gamma2 = 0.0 at every bin -- both positions still gate-passed
    // (coherence.has_value()), so that band's weight sums to zero WITH
    // contributors present: exactly the NoWeight case record §3 defines, not
    // NoContributor.
    constexpr std::size_t kZeroBand = 3;
    std::vector<float> gamma2(bandLayout.size(), 0.9f);
    gamma2[kZeroBand] = 0.0f;

    const std::vector<double> u{1.0, 1.0};
    const std::vector<MtwResult> positions{makePosition(cfg, bandLayout, gamma2),
                                            makePosition(cfg, bandLayout, gamma2)};

    const auto combined = spatialAverageMtw(positions, u);
    REQUIRE(combined.has_value());  // other bands are Present, so the whole result exists

    for (std::size_t b = 0; b < bandLayout.size(); ++b) {
        CAPTURE(b);
        const auto& band = bandLayout[b];
        for (std::size_t bin = band.firstBin; bin <= band.lastBin; ++bin) {
            const std::size_t index = band.firstIndex + (bin - band.firstBin);
            CAPTURE(bin, index);
            const auto& state = combined->bins[index];

            // THE FIX: contributors must read 2 at every bin of every band --
            // both positions passed the gate and have u_i > 0 everywhere,
            // whether or not their weight happens to sum to zero.
            REQUIRE(state.contributors == 2);

            if (b == kZeroBand) {
                REQUIRE(state.absence == SpatialAbsence::NoWeight);
            } else {
                REQUIRE(state.absence == SpatialAbsence::Present);
                // magnitude present elsewhere: |H|=1 at every contributing
                // position -> 20*log10(1) = 0 dB exactly.
                REQUIRE(combined->magnitudeDb[index] == Catch::Approx(0.0).margin(1e-6));
            }

            // No NaN anywhere, in any array, at any bin -- the defect this
            // guards against was a placeholder, not a NaN, but a future
            // regression in either path must not introduce one.
            REQUIRE(std::isfinite(combined->magnitudeDb[index]));
            REQUIRE(std::isfinite(combined->phaseRadians[index]));
            REQUIRE(std::isfinite(combined->phaseAgreement[index]));
            REQUIRE(std::isfinite(combined->weightedCoherence[index]));
        }
    }
}

TEST_CASE("every band absent leaves the whole MTW average absent", "[spatial][mtw][absence]") {
    const MtwConfig cfg;
    const auto bandLayout = mtwBands(cfg);

    // Every band's gamma2 is exactly 0.0 -- both positions gate-passed, so
    // every bin of every band is NoWeight, never NoContributor. The
    // all-absent -> nullopt rule (record §3, plan A1 rule 5) must still fire,
    // applied ONCE over the whole stitched result now that per-band
    // collapsing is gone.
    std::vector<float> gamma2(bandLayout.size(), 0.0f);

    const std::vector<double> u{1.0, 1.0};
    const std::vector<MtwResult> positions{makePosition(cfg, bandLayout, gamma2),
                                            makePosition(cfg, bandLayout, gamma2)};

    const auto combined = spatialAverageMtw(positions, u);
    REQUIRE_FALSE(combined.has_value());
}
