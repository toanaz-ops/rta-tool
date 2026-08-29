// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Analyser is the pure engine-driver: SpectrumEngine + BandWeights + the
// dBFS convention from Levels.h, published as one immutable Snapshot. Every
// expectation below traces back to a closed form already proven in
// core/tests (Parseval for a bin-centred tone, the IEC 61260 design-goal
// response, the pink generator's exact -3.0103 dB/octave slope) or to a
// rule this plan fixes (§1.4's dBFS convention, trap T-2's density()-not-
// spectrum() rule, the record's one-struct atomic-swap rule).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "measure/Analyser.h"

#include "rta/dsp/BandWeights.h"
#include "rta/dsp/OctaveBands.h"
#include "rta/gen/Synthetic.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::measure;

namespace {

Analyser::Config config48k4096() {
    Analyser::Config c;
    c.fftSize = 4096;
    c.hopSize = 1024;
    c.sampleRate = 48000.0;
    c.fraction = 3;
    c.lowHz = 20.0;
    c.highHz = 20000.0;
    c.window = rta::dsp::WindowType::Hann;
    c.averaging = rta::dsp::Averaging::Linear;
    return c;
}

/// The index of the band whose centreHz is closest to `hz`.
std::size_t closestBand(const Snapshot& snap, double hz) {
    std::size_t closest = 0;
    double bestDiff = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < snap.bands.size(); ++i) {
        const double diff = std::abs((double) snap.bands[i].centreHz - hz);
        if (diff < bestDiff) {
            bestDiff = diff;
            closest = i;
        }
    }
    return closest;
}

}  // namespace

TEST_CASE("A 1 kHz sine at -20 dBFS reads -20 dB in its own band", "[analyser]") {
    const auto config = config48k4096();
    Analyser analyser(config);

    // amplitude 0.1 is -20 dBFS by the sine-referenced convention (plan
    // §1.4): 20*log10(0.1) == -20.0.
    rta::gen::SyntheticSine sine(config.sampleRate, 1000.0, -20.0);
    std::vector<float> samples((std::size_t) (4.0 * config.sampleRate));
    sine.render(samples);
    analyser.pushMeasurement(samples);

    const auto snap = analyser.publish(0);
    const std::size_t closest = closestBand(*snap, 1000.0);

    CHECK_THAT((double) snap->bands[closest].levelDb, WithinAbs(-20.0, 0.1));
}

TEST_CASE("Neighbouring bands do not see the tone", "[analyser]") {
    const auto config = config48k4096();
    Analyser analyser(config);
    constexpr double toneHz = 1000.0;

    rta::gen::SyntheticSine sine(config.sampleRate, toneHz, -20.0);
    std::vector<float> samples((std::size_t) (4.0 * config.sampleRate));
    sine.render(samples);
    analyser.pushMeasurement(samples);

    const auto snap = analyser.publish(0);
    const std::size_t closest = closestBand(*snap, toneHz);

    // closed-form: BandWeights::responseAt is the IEC 61260-1 design-goal
    // response the whole class implements (asserted to 1e-12 against the
    // published formula in core/tests/test_bandweights.cpp), so the
    // attenuation a NEIGHBOUR's own weight function applies to a tone
    // sitting at THIS band's centre is not a guessed round number -- for
    // 1/3-octave order-6 Butterworth it computes to ~-18.30 dB at the very
    // next band, well short of a naive "-20 dB" guess but exactly what the
    // filter mask predicts. 0.5 dB of slack covers the window's mainlobe
    // spreading the tone over a few neighbouring bins instead of one.
    const rta::dsp::OctaveBands bands(config.fraction, config.lowHz, config.highHz);
    auto expectedNeighbourDb = [&](std::size_t neighbourIndex) {
        const double response = rta::dsp::BandWeights::responseAt(toneHz, bands[neighbourIndex]);
        return (double) snap->bands[closest].levelDb + 10.0 * std::log10(response);
    };

    if (closest > 0) {
        CHECK_THAT((double) snap->bands[closest - 1].levelDb,
                   WithinAbs(expectedNeighbourDb(closest - 1), 0.5));
    }
    if (closest + 1 < snap->bands.size()) {
        CHECK_THAT((double) snap->bands[closest + 1].levelDb,
                   WithinAbs(expectedNeighbourDb(closest + 1), 0.5));
    }
}

TEST_CASE("Pink noise gives a flat third-octave spectrum", "[analyser]") {
    auto config = config48k4096();
    Analyser analyser(config);

    // Frequency-domain pink (rta::gen::SyntheticPink), exact -3.0103 dB/oct
    // by construction -- so equal energy per fractional octave is exact, and
    // the only slack left is Linear-averaging's estimator variance.
    //
    // The block must be longer than the whole render: SyntheticPink's block
    // is built ONCE with a fixed magnitude per bin (only phase is random),
    // so once it loops, hop=1024 draws the exact same finite set of Welch
    // frames over and over -- at the plan's suggested 65536-sample block
    // that is only 64 distinct frames no matter how long the render runs,
    // whose standard error (~4.34/sqrt(64) =~ 0.54 dB) is exactly what blew
    // the ±0.5 dB tolerance below. A block that never wraps, plus enough
    // duration for real margin under that tolerance, keeps the estimator's
    // standard error comfortably under it (~750 frames -> ~0.16 dB, versus
    // ~0.5 dB budget) instead of sitting right at the edge of a fixed seed's
    // luck.
    constexpr std::size_t blockSize = 1 << 20;  // 1048576, > 16 s at 48 kHz
    rta::gen::SyntheticPink pink(blockSize, /*levelDbFs=*/-10.0, /*seed=*/0x5EEDu);

    const std::size_t totalSamples = (std::size_t) (16.0 * config.sampleRate);
    std::vector<float> samples(totalSamples);
    pink.render(samples);
    analyser.pushMeasurement(samples);

    const auto snap = analyser.publish(0);
    REQUIRE(analyser.framesAnalysed() >= 350);

    // Excludes underResolved bands deliberately: at this fftSize/sampleRate
    // (4096 @ 48 kHz, 11.72 Hz bins) the 50 and 63 Hz thirds span only 1-2
    // bins each, and "Under-resolved bands are flagged..." already proves
    // that -- it is a bin-count quantisation limit of the transform itself,
    // not something more Linear-averaging frames fixes (measured: going
    // from 372 to 747 frames left those two bands' error unchanged). Ask a
    // band the transform provably cannot resolve to also read a flat pink
    // spectrum and no implementation of this fftSize can pass.
    std::vector<double> selected;
    for (const auto& band : snap->bands) {
        if (band.centreHz >= 50.0f && band.centreHz <= 10000.0f && !band.underResolved) {
            selected.push_back((double) band.levelDb);
        }
    }
    REQUIRE(!selected.empty());

    double mean = 0.0;
    for (const double v : selected) mean += v;
    mean /= (double) selected.size();

    for (const double v : selected) {
        CHECK_THAT(v, WithinAbs(mean, 0.5));
    }
}

TEST_CASE("Under-resolved bands are flagged below the transform's limit", "[analyser]") {
    auto config = config48k4096();
    config.fftSize = 1024;
    config.hopSize = 512;
    Analyser analyser(config);

    // underResolved is a property of the band/bin geometry fixed at
    // construction (BandWeights::kMinBinsPerBand), independent of what
    // publish() has seen -- so an empty run is enough to check it.
    const auto snap = analyser.publish(0);

    const double binWidthHz = config.sampleRate / (double) config.fftSize;  // 46.875 Hz
    for (const auto& band : snap->bands) {
        const double span = (double) band.upperHz - (double) band.lowerHz;
        CAPTURE(band.centreHz, span);
        CHECK(band.underResolved == (span < 3.0 * binWidthHz));
    }
}

TEST_CASE("Consecutive publishes hand out different immutable pointers", "[analyser]") {
    Analyser analyser(config48k4096());

    const auto snap1 = analyser.publish(111);
    const auto snap2 = analyser.publish(222);

    CHECK(snap1.get() != snap2.get());
    CHECK(snap2->sequence == snap1->sequence + 1);
    // snap1's contents must be unaffected by the second publish -- a reader
    // holding it across a frame is the whole point of the atomic swap.
    CHECK(snap1->droppedSamples == 111);
    CHECK(snap2->droppedSamples == 222);
}

TEST_CASE("A snapshot's band count matches the band definition", "[analyser]") {
    const auto config = config48k4096();
    Analyser analyser(config);
    const auto snap = analyser.publish(0);

    const rta::dsp::OctaveBands bands(config.fraction, config.lowHz, config.highHz);
    CHECK(snap->bands.size() == bands.size());
    CHECK(snap->spectrumDb.size() == config.fftSize / 2 + 1);
}

TEST_CASE("Dropped samples are carried into the snapshot", "[analyser]") {
    Analyser analyser(config48k4096());
    const auto snap = analyser.publish(1234);
    CHECK(snap->droppedSamples == 1234);
}

// ---------------------------------------------------------------------------
// Transfer function -- the app-side bridge to rta::dsp::DualFftEngine.
// ---------------------------------------------------------------------------

namespace {

/// Deterministic broadband source. Pink rather than white only because it is
/// what the rest of this file already uses; the transfer function of a pure
/// delay is the same either way.
///
/// `SyntheticPink(blockSize, levelDbFs, seed)` -- a looping block, not an
/// endless stream. That the block repeats does not disturb any assertion here:
/// both channels are drawn from the SAME sequence, one shifted, so
/// y[n] = x[n-D] holds across the loop seam exactly as it does everywhere
/// else, and H = exp(-j*omega*D) with it.
std::vector<float> pinkBlock(std::size_t n, std::uint32_t seed) {
    rta::gen::SyntheticPink pink(8192, -20.0, seed);
    std::vector<float> out(n);
    pink.render(out);
    return out;
}

/// `source` delayed by `delaySamples`, zero-filled at the front. Same length,
/// so the two spans a dual-FFT is handed are the same time instant -- which is
/// the one precondition DualFftEngine refuses to guess about.
std::vector<float> delayed(std::span<const float> source, int delaySamples) {
    std::vector<float> out(source.size(), 0.0f);
    for (std::size_t i = static_cast<std::size_t>(delaySamples); i < source.size(); ++i) {
        out[i] = source[i - static_cast<std::size_t>(delaySamples)];
    }
    return out;
}

rta::measure::Analyser::Config transferConfig() {
    rta::measure::Analyser::Config cfg;
    cfg.fftSize = 4096;
    cfg.hopSize = 2048;
    cfg.sampleRate = 48000.0;
    return cfg;
}

}  // namespace

TEST_CASE("a measurement with no reference carries no transfer function",
          "[analyser][transfer]") {
    // Absence of a reference is absence of a transfer function -- not a flat
    // 0 dB one. A single-channel RTA capture must not produce a Bode plot of
    // itself against nothing.
    rta::measure::Analyser analyser(transferConfig());
    const auto block = pinkBlock(48000, 0x5EEDu);
    analyser.pushMeasurement(block);
    const auto snapshot = analyser.publish(0);

    REQUIRE(snapshot != nullptr);
    CHECK_FALSE(snapshot->transfer.has_value());
}

TEST_CASE("a compensated delay leaves the phase flat", "[analyser][transfer]") {
    // The engine applies referenceDelaySamples BEFORE the transform, as an
    // integer stream offset (dual-FFT record section 4). Compensating a known
    // 4-sample lag must therefore return phase to zero across the band -- and
    // magnitude to 0 dB, since nothing but a delay was applied.
    auto cfg = transferConfig();
    cfg.referenceDelaySamples = 4;

    rta::measure::Analyser analyser(cfg);
    const auto reference = pinkBlock(48000 * 2, 0x5EEDu);
    const auto measurement = delayed(reference, 4);
    analyser.pushPair(reference, measurement);
    const auto snapshot = analyser.publish(0);

    REQUIRE(snapshot != nullptr);
    REQUIRE(snapshot->transfer.has_value());
    const auto& tf = *snapshot->transfer;
    REQUIRE(tf.phaseDeg.size() == snapshot->fftSize / 2 + 1);

    const double binHz = cfg.sampleRate / static_cast<double>(cfg.fftSize);
    for (const double hz : { 1000.0, 2000.0, 4000.0 }) {
        const auto bin = static_cast<std::size_t>(std::llround(hz / binHz));
        CHECK(std::abs(tf.phaseDeg[bin]) < 2.0f);
        CHECK(std::abs(tf.magnitudeDb[bin]) < 1.0f);
    }
}

TEST_CASE("an uncompensated delay gives the closed-form phase slope",
          "[analyser][transfer]") {
    // phi(f) = -2*pi*f*D/fs, i.e. -360*f*D/fs degrees. With D = 4 at 48 kHz
    // that is -30 deg at 1 kHz, -60 at 2 kHz and -120 at 4 kHz -- none of them
    // near the +-180 wrap, so the assertion tests the SLOPE and not the
    // wrapping. This is the identity, not a number the implementation printed.
    auto cfg = transferConfig();
    cfg.referenceDelaySamples = 0;

    rta::measure::Analyser analyser(cfg);
    const auto reference = pinkBlock(48000 * 2, 0x5EEDu);
    const auto measurement = delayed(reference, 4);
    analyser.pushPair(reference, measurement);
    const auto snapshot = analyser.publish(0);

    REQUIRE(snapshot->transfer.has_value());
    const auto& tf = *snapshot->transfer;
    const double binHz = cfg.sampleRate / static_cast<double>(cfg.fftSize);

    for (const double hz : { 1000.0, 2000.0, 4000.0 }) {
        const auto bin = static_cast<std::size_t>(std::llround(hz / binHz));
        const double expected = -360.0 * (static_cast<double>(bin) * binHz) * 4.0 / cfg.sampleRate;
        CHECK(static_cast<double>(tf.phaseDeg[bin]) == Catch::Approx(expected).margin(2.0));
    }
}

TEST_CASE("phase crosses the seam in degrees, not radians", "[analyser][transfer]") {
    // Cheap, and it genuinely falsifies. core hands out radians, wrapped to
    // (-pi, pi], so no radian value can exceed 3.15 -- while the case above
    // expects -120 degrees at 4 kHz. A forgotten conversion turns the whole
    // phase pane into a flat line at the middle of the axis, which looks like
    // a perfectly aligned system rather than like a bug.
    auto cfg = transferConfig();
    rta::measure::Analyser analyser(cfg);
    const auto reference = pinkBlock(48000 * 2, 0x5EEDu);
    analyser.pushPair(reference, delayed(reference, 4));
    const auto snapshot = analyser.publish(0);

    REQUIRE(snapshot->transfer.has_value());
    const auto& phase = snapshot->transfer->phaseDeg;
    const bool anyBeyondRadians =
        std::any_of(phase.begin(), phase.end(), [](float p) { return std::abs(p) > 3.2f; });
    CHECK(anyBeyondRadians);
}

TEST_CASE("coherence is withheld until enough averages exist",
          "[analyser][transfer]") {
    // dual-FFT record section 3: for a single frame |X*Y|^2 == |X|^2|Y|^2
    // identically, so coherence is exactly 1.0 at every frequency and a
    // completely broken engine looks perfect. The gate lives in core's
    // makeSnapshot and is guarded by coherence_gate_is_not_bypassed; this
    // asserts the app-side bridge PROPAGATES the absence rather than
    // substituting an empty vector or a 1.0.
    auto cfg = transferConfig();

    rta::measure::Analyser thin(cfg);
    const auto shortBlock = pinkBlock(cfg.fftSize + cfg.hopSize, 0x5EEDu);
    thin.pushPair(shortBlock, delayed(shortBlock, 4));
    const auto thinSnapshot = thin.publish(0);
    REQUIRE(thinSnapshot->transfer.has_value());
    CHECK_FALSE(thinSnapshot->transfer->coherence.has_value());

    rta::measure::Analyser thick(cfg);
    const auto longBlock = pinkBlock(48000 * 2, 0x5EEDu);
    thick.pushPair(longBlock, delayed(longBlock, 4));
    const auto thickSnapshot = thick.publish(0);
    REQUIRE(thickSnapshot->transfer.has_value());
    REQUIRE(thickSnapshot->transfer->coherence.has_value());

    // Two identical channels differing only by a delay ARE fully coherent, so
    // this is the honest expectation here -- and it is exactly why the
    // low-coherence display test in task 8 uses added noise instead.
    const auto& coherence = *thickSnapshot->transfer->coherence;
    const double binHz = cfg.sampleRate / static_cast<double>(cfg.fftSize);
    const auto bin = static_cast<std::size_t>(std::llround(1000.0 / binHz));
    CHECK(coherence[bin] == Catch::Approx(1.0).margin(0.05));
    CHECK(thickSnapshot->transfer->effectiveAverages > 8.0);
}

TEST_CASE("reset drops the transfer function with everything else",
          "[analyser][transfer]") {
    // A mid-run device change must not splice frames from two different device
    // sessions into one averaged H -- the same reason
    // AnalysisThread::rebuildAnalyserIfEpochChanged exists.
    rta::measure::Analyser analyser(transferConfig());
    const auto block = pinkBlock(48000 * 2, 0x5EEDu);
    analyser.pushPair(block, delayed(block, 4));
    REQUIRE(analyser.publish(0)->transfer.has_value());

    analyser.reset();
    CHECK_FALSE(analyser.publish(0)->transfer.has_value());
}
