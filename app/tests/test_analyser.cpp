// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Analyser is the pure engine-driver: SpectrumEngine + BandWeights + the
// dBFS convention from Levels.h, published as one immutable Snapshot. Every
// expectation below traces back to a closed form already proven in
// core/tests (Parseval for a bin-centred tone, the IEC 61260 design-goal
// response, the pink generator's exact -3.0103 dB/octave slope) or to a
// rule this plan fixes (§1.4's dBFS convention, trap T-2's density()-not-
// spectrum() rule, the record's one-struct atomic-swap rule).

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
