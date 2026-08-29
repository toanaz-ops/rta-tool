// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests. The app-side bridge from rta::dsp's dual-FFT
// engine to measure::Snapshot.
//
// Split out of test_analyser.cpp rather than appended to it: that file was
// already 396 lines against the project's 400-line hard cap, and these cases
// bring their own helpers.
#include "measure/Analyser.h"

#include "rta/gen/Synthetic.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

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
    // A cheap backstop for ONE specific bug, not a general check. core hands
    // out radians wrapped to (-pi, pi], so no radian value can exceed 3.15,
    // while the case above expects -120 degrees at 4 kHz; a forgotten
    // conversion turns the phase pane into a flat line at the middle of the
    // axis, which looks like a perfectly aligned system rather than a bug.
    //
    // On its own this proves little -- any large garbage passes it. The
    // closed-form slope case two above is what pins the actual values; this
    // one is here because the radians bug is the one a reader is most likely
    // to reintroduce, and it names that bug at the point of failure.
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

TEST_CASE("a paired push still drives the band readouts",
          "[analyser][transfer]") {
    // pushPair feeds the two SpectrumEngines as well as the dual-FFT engine,
    // so a two-channel session still shows bars and a reference. Nothing
    // asserted that until now: deleting both SpectrumEngine::process calls
    // from pushPair left every other test in this file green, because they all
    // read the transfer block and none reads the bands.
    rta::measure::Analyser analyser(transferConfig());
    const auto reference = pinkBlock(48000, 0x5EEDu);
    analyser.pushPair(reference, delayed(reference, 4));
    const auto snapshot = analyser.publish(0);

    REQUIRE(snapshot != nullptr);
    CHECK(snapshot->hasReference);
    CHECK_FALSE(snapshot->referenceBands.empty());
    CHECK(snapshot->framesAnalysed > 0u);
    // The bands are a real reading, not a floor: pink noise at -20 dBFS has
    // energy in every band this analyser covers.
    CHECK(snapshot->peakBandLevelDb > static_cast<float>(rta::measure::kLevelFloorDb));
}

TEST_CASE("mismatched spans are refused before anything is fed",
          "[analyser][transfer]") {
    // The one precondition a dual-FFT cannot guess about is that its two spans
    // are the same time instant. Refusing it AFTER feeding the spectrum
    // engines would leave a half-accepted pair behind: bands advanced,
    // hasReference latched, from a call whose contract says it was refused. A
    // caller that catches and carries on would then publish readouts
    // contaminated by a call it believes never happened.
    rta::measure::Analyser analyser(transferConfig());
    const auto reference = pinkBlock(4096, 0x5EEDu);
    const auto shorter = pinkBlock(2048, 0x5EEDu);

    CHECK_THROWS_AS(analyser.pushPair(reference, shorter), std::invalid_argument);

    const auto snapshot = analyser.publish(0);
    REQUIRE(snapshot != nullptr);
    CHECK_FALSE(snapshot->hasReference);
    CHECK_FALSE(snapshot->transfer.has_value());
    CHECK(snapshot->framesAnalysed == 0u);
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
