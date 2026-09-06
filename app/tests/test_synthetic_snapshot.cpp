// SPDX-License-Identifier: AGPL-3.0-or-later
//
// makeSyntheticSnapshot is the offline, deterministic path that feeds both
// `rta-view.png` (tools/snapshot.cpp) and CI: no threads, no timing, so a
// given SyntheticSpec must always produce the same Snapshot -- that is the
// precondition for the PNG being reviewable as a byte-for-byte diff.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "measure/SyntheticSnapshot.h"

#include <cmath>
#include <cstddef>

using namespace rta::measure;

namespace {

SyntheticSpec fastSpec() {
    SyntheticSpec spec;
    spec.analysis.sampleRate = 48000.0;
    spec.analysis.fftSize = 4096;
    spec.analysis.hopSize = 1024;
    spec.seconds = 2.0;  // enough frames without slowing the suite
    return spec;
}

}  // namespace

TEST_CASE("The same spec produces the identical snapshot", "[synthetic-snapshot]") {
    const auto spec = fastSpec();
    const auto a = makeSyntheticSnapshot(spec);
    const auto b = makeSyntheticSnapshot(spec);

    REQUIRE(a->bands.size() == b->bands.size());
    for (std::size_t i = 0; i < a->bands.size(); ++i) {
        CAPTURE(i);
        CHECK(a->bands[i].levelDb == b->bands[i].levelDb);
    }
    REQUIRE(a->spectrumDb.size() == b->spectrumDb.size());
    for (std::size_t i = 0; i < a->spectrumDb.size(); ++i) {
        CHECK(a->spectrumDb[i] == b->spectrumDb[i]);
    }
}

TEST_CASE("A different seed produces a different snapshot", "[synthetic-snapshot]") {
    auto spec = fastSpec();
    const auto a = makeSyntheticSnapshot(spec);
    spec.seed ^= 1u;
    const auto b = makeSyntheticSnapshot(spec);

    bool anyDifferent = false;
    for (std::size_t i = 0; i < a->bands.size(); ++i) {
        if (a->bands[i].levelDb != b->bands[i].levelDb) {
            anyDifferent = true;
            break;
        }
    }
    CHECK(anyDifferent);
}

TEST_CASE("The default synthetic spec is a usable picture", "[synthetic-snapshot]") {
    const SyntheticSpec spec;  // every default, including 4.0 s of pink noise
    const auto snap = makeSyntheticSnapshot(spec);

    CHECK(snap->bands.size() >= 20);
    CHECK(snap->peakBandLevelDb >= -60.0f);
    CHECK(snap->peakBandLevelDb <= 0.0f);

    for (const auto& band : snap->bands) {
        CHECK(std::isfinite(band.levelDb));
        if (band.centreHz >= 50.0f && band.centreHz <= 10000.0f) {
            CHECK(band.levelDb > -120.0f);
        }
    }
}

// ---------------------------------------------------------------------
// makeSyntheticTransfer: the per-bin fixture behind transfer.png. Every
// claim below is independently checkable (a closed form or a length/range
// fact) rather than pinning whatever the implementation happens to print --
// project CLAUDE.md's verification standard applies to a fixture exactly as
// much as to production DSP, because a wrong fixture would make transfer.png
// look right for the wrong reason.
// ---------------------------------------------------------------------

namespace {

/// A copy of the fixture's own (-180, 180] wrap (SyntheticSnapshot.cpp's
/// `wrapDegrees180`), NOT an independent implementation: a bug shared by both
/// copies of the wrap convention itself would still agree here. What this
/// copy actually buys is not having to include the production header for one
/// static function; the real assertion below is the closed-form phase slope
/// (`-360*hz*D/fs`), which this function only wraps into the same range the
/// fixture's output is already in -- any sign, scale, or unit error in the
/// slope itself has nothing to do with the wrap and is what the test
/// actually catches.
float wrapDeg(double deg) {
    double w = std::fmod(deg + 180.0, 360.0);
    if (w <= 0.0) w += 360.0;
    return static_cast<float>(w - 180.0);
}

}  // namespace

// CATCHES: an off-by-one in the bin count (fftSize/2 instead of fftSize/2+1,
// or vice versa), which would desync every downstream column-mapping index
// against the coherence/phase arrays by one bin.
TEST_CASE("makeSyntheticTransfer's arrays are fftSize/2 + 1 long", "[synthetic-transfer]") {
    constexpr std::size_t kFftSize = 2048;
    const auto block = makeSyntheticTransfer(kFftSize, 48000.0, 20);

    const std::size_t expected = kFftSize / 2 + 1;
    CHECK(block.magnitudeDb.size() == expected);
    CHECK(block.phaseDeg.size() == expected);
    REQUIRE(block.coherence.has_value());
    CHECK(block.coherence->size() == expected);
}

// CATCHES: a phase generator that used radians where Snapshot.h's contract
// is degrees, one that forgot the minus sign (measurement LEADING the
// reference instead of lagging it), or one that wrapped with the wrong
// convention (e.g. [0, 360) instead of (-180, 180]) -- any of those would
// diverge from the closed form at bins far enough out to have wrapped at
// least once.
TEST_CASE("makeSyntheticTransfer's phase matches the pure-delay closed form", "[synthetic-transfer]") {
    constexpr std::size_t kFftSize = 4096;
    constexpr double kSampleRate = 48000.0;
    constexpr int kDelaySamples = 25;
    const auto block = makeSyntheticTransfer(kFftSize, kSampleRate, kDelaySamples);

    const double binHz = kSampleRate / static_cast<double>(kFftSize);
    // A handful of bins spread across the range, including some past the
    // point where -360*f*D/fs has wrapped more than once (kDelaySamples=25
    // over a 4096-point FFT wraps well before Nyquist).
    for (const std::size_t k : { std::size_t{0}, std::size_t{10}, std::size_t{100}, std::size_t{500},
                                 std::size_t{1000}, kFftSize / 2 }) {
        CAPTURE(k);
        const double hz = static_cast<double>(k) * binHz;
        const double exact = -360.0 * hz * static_cast<double>(kDelaySamples) / kSampleRate;
        CHECK(block.phaseDeg[k] == Catch::Approx(wrapDeg(exact)).margin(1e-3));
    }
}

// CATCHES: a coherence curve that is flat (no dip modelled at all -- the
// fixture would then never exercise the ribbon's fade or the alpha-differs
// test in test_transfer_view.cpp), and one that leaks outside [0, 1] (which
// alphaForCoherence's own contract assumes never happens for a sane
// producer).
TEST_CASE("makeSyntheticTransfer's coherence dips at the notch and stays in [0, 1]",
          "[synthetic-transfer]") {
    constexpr std::size_t kFftSize = 4096;
    constexpr double kSampleRate = 48000.0;
    const auto block = makeSyntheticTransfer(kFftSize, kSampleRate, 20);
    REQUIRE(block.coherence.has_value());

    const double binHz = kSampleRate / static_cast<double>(kFftSize);
    const auto bandCoherenceAt = [&](double targetHz) {
        const auto k = static_cast<std::size_t>(std::llround(targetHz / binHz));
        return (*block.coherence)[k];
    };

    const float midbandCoherence = bandCoherenceAt(1000.0);   // flat region
    const float notchCoherence = bandCoherenceAt(2000.0);     // the dip's centre

    CHECK(notchCoherence < midbandCoherence);

    for (const float g : *block.coherence) {
        CHECK(g >= 0.0f);
        CHECK(g <= 1.0f);
    }
}

// CATCHES: any hidden state -- an uninitialised scratch buffer, a static
// counter, an RNG seeded from wall-clock time -- that would make transfer.png
// non-reproducible across two runs of rtatool_snapshot on the same machine.
TEST_CASE("makeSyntheticTransfer is deterministic", "[synthetic-transfer]") {
    const auto a = makeSyntheticTransfer(2048, 48000.0, 17);
    const auto b = makeSyntheticTransfer(2048, 48000.0, 17);

    REQUIRE(a.magnitudeDb.size() == b.magnitudeDb.size());
    for (std::size_t i = 0; i < a.magnitudeDb.size(); ++i) {
        CHECK(a.magnitudeDb[i] == b.magnitudeDb[i]);
        CHECK(a.phaseDeg[i] == b.phaseDeg[i]);
    }
    REQUIRE(a.coherence.has_value());
    REQUIRE(b.coherence.has_value());
    REQUIRE(a.coherence->size() == b.coherence->size());
    for (std::size_t i = 0; i < a.coherence->size(); ++i) {
        CHECK((*a.coherence)[i] == (*b.coherence)[i]);
    }
}

// ---------------------------------------------------------------------
// makeSyntheticMtw: the per-band fixture behind the MTW half of transfer.png
// and workspace.png (docs/reports/005-mtw-engine.md "Known gaps": "no
// committed snapshot fixture carries an MtwBlock"). Point count and seam
// frequencies are checked against the SAME closed forms
// test_analyser_mtw.cpp checks the real engine against -- 1281 points, seams
// at 187.5/375/750/1500/3000/6000 Hz for the default MtwConfig -- not a
// number this fixture invented for itself.
// ---------------------------------------------------------------------

// CATCHES: an off-by-one in mtwFrequencies' own point count reaching this
// fixture wrong, or a fixture that filled its arrays from the wrong source
// (fftSize/2+1 instead of the MTW engine's own stitched length).
TEST_CASE("makeSyntheticMtw's arrays are all mtwPointCount long and strictly increasing",
          "[synthetic-mtw]") {
    const auto block = makeSyntheticMtw();

    REQUIRE(block.frequencyHz.size() == 1281);
    CHECK(block.magnitudeDb.size() == 1281);
    CHECK(block.phaseDeg.size() == 1281);
    CHECK(block.coherence.size() == 1281);

    for (std::size_t i = 1; i < block.frequencyHz.size(); ++i) {
        CAPTURE(i);
        CHECK(block.frequencyHz[i] > block.frequencyHz[i - 1]);
    }
}

// CATCHES: band descriptors built from a hand-picked table instead of
// rta::dsp::mtwBands -- a seam at the wrong frequency, or a band marked
// available before its coherence is meant to exist.
TEST_CASE("makeSyntheticMtw's band descriptors carry the record's own seam frequencies",
          "[synthetic-mtw]") {
    const auto block = makeSyntheticMtw();
    REQUIRE(block.bands.size() == 7);

    const std::vector<float> expectedSeamHz{ 0.0f, 187.5f, 375.0f, 750.0f, 1500.0f, 3000.0f, 6000.0f };
    std::size_t totalPoints = 0;
    for (std::size_t i = 0; i < block.bands.size(); ++i) {
        CAPTURE(i);
        CHECK(block.bands[i].seamHz == Catch::Approx(expectedSeamHz[i]));
        CHECK(block.bands[i].coherenceAvailable);
        totalPoints += block.bands[i].pointCount;
    }
    CHECK(totalPoints == 1281);
}

// CATCHES: hidden state (an uninitialised buffer, a static counter) that
// would make the MTW half of transfer.png non-reproducible across runs, the
// same property makeSyntheticTransfer already guarantees for the fixed half.
TEST_CASE("makeSyntheticMtw is deterministic", "[synthetic-mtw]") {
    const auto a = makeSyntheticMtw();
    const auto b = makeSyntheticMtw();

    REQUIRE(a.frequencyHz.size() == b.frequencyHz.size());
    for (std::size_t i = 0; i < a.frequencyHz.size(); ++i) {
        CHECK(a.frequencyHz[i] == b.frequencyHz[i]);
        CHECK(a.magnitudeDb[i] == b.magnitudeDb[i]);
        CHECK(a.phaseDeg[i] == b.phaseDeg[i]);
        CHECK(a.coherence[i] == b.coherence[i]);
    }
}
