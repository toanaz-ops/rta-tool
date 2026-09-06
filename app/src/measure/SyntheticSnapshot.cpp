// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// See docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.3, §3.4.
#include "measure/SyntheticSnapshot.h"

#include "measure/Levels.h"

#include "rta/gen/Synthetic.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rta::measure {

namespace {

constexpr std::size_t kChunkSamples = 1024;

/// A pink block size must be a power of two, and this fixture always wants
/// the same one for a given fftSize so the loop period lines up predictably
/// with the analysis frame size. Any power-of-two >= 4 would satisfy
/// SyntheticPink's contract; matching fftSize is simplest and needs no
/// extra field on SyntheticSpec.
std::size_t pinkBlockSize(const Analyser::Config& config) {
    return std::max<std::size_t>(config.fftSize, 4);
}

// ---------------------------------------------------------------------
// makeSyntheticTransfer's closed forms. Deliberately small, local copies of
// the shapes app/src/dev/preview/PreviewMath.h uses for the mockup -- not a
// shared include, because `dev/preview` is a demo layer that depends on
// `measure/`, never the other way around, and this file is scanned by the
// measure_has_no_framework_deps guard where PreviewMath.h is not.
// ---------------------------------------------------------------------

/// Hermite smoothstep: 0 below `lo`, 1 above `hi`, an S-curve between.
double smoothstep(double x, double lo, double hi) {
    if (lo == hi) return x < lo ? 0.0 : 1.0;
    const double t = std::clamp((x - lo) / (hi - lo), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

/// A dip (or, with a negative `depthDb`, a peak) shaped as a gaussian in LOG
/// frequency -- see PreviewMath.h's identical function for why octaves are
/// the natural width unit.
double logGaussianDb(double hz, double centreHz, double widthOctaves, double depthDb) {
    const double x = std::log2(hz / centreHz) / widthOctaves;
    return -depthDb * std::exp(-0.5 * x * x);
}

/// The notch every closed-form curve below shares (magnitude AND
/// coherence): the same 2 kHz problem the L5 preview mockups already draw,
/// so the picture this fixture produces stays recognisable against them.
constexpr double kNotchCentreHz = 2000.0;
constexpr double kNotchWidthOctaves = 0.35;
constexpr double kNotchDepthDb = 8.0;

constexpr double kLfShelfLowHz = 40.0;
constexpr double kLfShelfHighHz = 250.0;
constexpr double kLfShelfGainDb = 5.5;

constexpr double kHfRolloffLowHz = 5000.0;
constexpr double kHfRolloffHighHz = 18000.0;
constexpr double kHfRolloffDepthDb = 12.0;

double magnitudeDbAt(double hz) {
    const double lf = kLfShelfGainDb * (1.0 - smoothstep(hz, kLfShelfLowHz, kLfShelfHighHz));
    const double notch = logGaussianDb(hz, kNotchCentreHz, kNotchWidthOctaves, kNotchDepthDb);
    const double hf = -kHfRolloffDepthDb * smoothstep(hz, kHfRolloffLowHz, kHfRolloffHighHz);
    return lf + notch + hf;
}

/// Wrap to `(-180, 180]`, matching core's own convention for
/// `TransferBlock::phaseDeg` (Snapshot.h).
float wrapDegrees180(double deg) {
    double wrapped = std::fmod(deg + 180.0, 360.0);
    if (wrapped <= 0.0) wrapped += 360.0;
    return static_cast<float>(wrapped - 180.0);
}

/// Coherence: high and flat through the midband, dipping at the LF end (low
/// SNR against room noise below ~80 Hz) and again at the notch (a dip is
/// low-energy, so the reference/response ratio there is noisier).
constexpr double kLfCoherenceDipEndHz = 80.0;
constexpr double kLfCoherenceFloor = 0.32;
constexpr double kNotchCoherenceFloor = 0.55;
constexpr double kCoherenceCeiling = 0.97;

double coherenceAt(double hz) {
    const double lfDip =
        (1.0 - smoothstep(hz, 25.0, kLfCoherenceDipEndHz)) * (kCoherenceCeiling - kLfCoherenceFloor);
    const double notchDip =
        -logGaussianDb(hz, kNotchCentreHz, kNotchWidthOctaves, kCoherenceCeiling - kNotchCoherenceFloor);
    return std::clamp(kCoherenceCeiling - lfDip - notchDip, 0.03, kCoherenceCeiling);
}

/// Effective averages a real capture might report after a few seconds of a
/// stable dual-FFT lock -- comfortably above any gate L5b might eventually
/// set, so this fixture's coherence is never mistaken for the single-frame
/// "identically 1.0" case dual-FFT record section 3 warns about.
constexpr double kSyntheticEffectiveAverages = 32.0;

}  // namespace

SnapshotPtr makeSyntheticSnapshot(const SyntheticSpec& spec) {
    Analyser analyser(spec.analysis);

    // Peak amplitude -> the project's sine-referenced dBFS, via the ONE
    // conversion Levels.h defines: mean square of a peak-A signal read the
    // same way a sine's is, amplitude^2/2.
    const double targetDbFs = levelDbFs(spec.amplitude * spec.amplitude * 0.5);

    const auto totalSamples =
        static_cast<std::size_t>(std::llround(spec.seconds * spec.analysis.sampleRate));

    std::vector<float> chunk(kChunkSamples);

    if (spec.source == SyntheticSpec::Source::Sine) {
        rta::gen::SyntheticSine sine(spec.analysis.sampleRate, spec.sineHz, targetDbFs);
        std::size_t remaining = totalSamples;
        while (remaining > 0) {
            const std::size_t n = std::min(kChunkSamples, remaining);
            std::span<float> block(chunk.data(), n);
            sine.render(block);
            analyser.pushMeasurement(block);
            remaining -= n;
        }
    } else {
        rta::gen::SyntheticPink pink(pinkBlockSize(spec.analysis), targetDbFs, spec.seed);
        std::size_t remaining = totalSamples;
        while (remaining > 0) {
            const std::size_t n = std::min(kChunkSamples, remaining);
            std::span<float> block(chunk.data(), n);
            pink.render(block);
            analyser.pushMeasurement(block);
            remaining -= n;
        }
    }

    // No threads, no timing: publish once, offline, at the end -- this is
    // what makes the result bit-identical for the same spec.
    return analyser.publish(0);
}

TransferBlock makeSyntheticTransfer(std::size_t fftSize, double sampleRate, int delaySamples) {
    TransferBlock block;

    const std::size_t bins = fftSize > 0 ? fftSize / 2 + 1 : 0;
    const double binHz = fftSize > 0 ? sampleRate / static_cast<double>(fftSize) : 0.0;

    block.magnitudeDb.resize(bins);
    block.phaseDeg.resize(bins);
    std::vector<float> coherence(bins);

    for (std::size_t k = 0; k < bins; ++k) {
        const double hz = static_cast<double>(k) * binHz;

        block.magnitudeDb[k] = static_cast<float>(magnitudeDbAt(hz));

        // A pure delay: phi(f) = -360 * f * D / fs, wrapped -- the exact
        // closed form test_synthetic_snapshot.cpp checks bin-by-bin, and the
        // same identity dual-FFT record section 4 / test_phase_unwrap.cpp
        // use (there in radians, here in degrees -- Snapshot.h's TransferBlock
        // is always degrees).
        const double phaseDeg = -360.0 * hz * static_cast<double>(delaySamples) / sampleRate;
        block.phaseDeg[k] = wrapDegrees180(phaseDeg);

        coherence[k] = static_cast<float>(coherenceAt(hz));
    }

    block.coherence = std::move(coherence);
    block.effectiveAverages = kSyntheticEffectiveAverages;
    // Nothing was compensated before this synthetic transform -- `delaySamples`
    // models the acoustic delay that SHOWS UP as the phase slope above, not a
    // stream offset an engine applied and would otherwise report here.
    block.appliedDelaySamples = 0;

    return block;
}

// 8.5866271: fifoEffectiveAverages(hann(N), N/4, 16) -- the record's own
// digits (docs/dsp/2026-09-05-mtw-l3.md §5) for a FULLY FILLED FIFO at the
// default depth, identical in every band because hop/N = 1/4 everywhere.
// test_analyser_mtw.cpp checks this same constant against the real engine;
// this fixture states a completed measurement, so it is never recomputed
// from a window here.
constexpr double kSyntheticMtwEffectiveAverages = 8.5866271;

MtwBlock makeSyntheticMtw(const rta::dsp::MtwConfig& config, int delaySamples) {
    MtwBlock block;
    block.frequencyHz = rta::dsp::mtwFrequencies(config);
    const std::size_t n = block.frequencyHz.size();

    block.magnitudeDb.resize(n);
    block.phaseDeg.resize(n);
    block.coherence.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double hz = block.frequencyHz[i];
        block.magnitudeDb[i] = static_cast<float>(magnitudeDbAt(hz));
        // The same pure-delay closed form makeSyntheticTransfer's phase uses,
        // evaluated at this point's own frequency rather than a fixed bin
        // width -- see this function's header comment for why the frequency
        // vector itself has to be per-point.
        const double phaseDeg = -360.0 * hz * static_cast<double>(delaySamples) / config.sampleRate;
        block.phaseDeg[i] = wrapDegrees180(phaseDeg);
        block.coherence[i] = static_cast<float>(coherenceAt(hz));
    }

    const auto bands = rta::dsp::mtwBands(config);
    block.bands.reserve(bands.size());
    for (const auto& band : bands) {
        MtwBandDescriptor descriptor;
        descriptor.firstIndex = band.firstIndex;
        descriptor.pointCount = band.lastBin - band.firstBin + 1;
        descriptor.fftSize = band.fftSize;
        descriptor.windowSeconds = static_cast<float>(band.windowSeconds);
        descriptor.integrationSeconds = static_cast<float>(band.integrationSeconds);
        descriptor.effectiveAverages = kSyntheticMtwEffectiveAverages;
        descriptor.seamHz = static_cast<float>(band.lowerEdgeHz);
        // A fully filled fixture (this function's own contract comment) --
        // every band has passed its own gate, unlike Analyser::publish's live
        // per-band fill (Snapshot.h's own comment on coherenceAvailable).
        descriptor.coherenceAvailable = true;
        block.bands.push_back(descriptor);
    }
    block.appliedDelaySamples = 0;

    return block;
}

std::pair<AverageBlock, std::vector<PositionSummary>> makeSyntheticAverage(std::size_t fftSize,
                                                                           double sampleRate,
                                                                           int positionCount) {
    AverageBlock average;
    const std::size_t bins = fftSize > 0 ? fftSize / 2 + 1 : 0;
    const double binHz = fftSize > 0 ? sampleRate / static_cast<double>(fftSize) : 0.0;

    average.magnitudeDb.resize(bins);
    average.phaseDeg.resize(bins);
    average.phaseAgreement.resize(bins);
    average.weightedCoherence.resize(bins);
    average.contributors.assign(bins, static_cast<std::uint16_t>(positionCount));
    average.absence.assign(bins, rta::dsp::SpatialAbsence::Present);

    for (std::size_t k = 0; k < bins; ++k) {
        const double hz = static_cast<double>(k) * binHz;
        average.magnitudeDb[k] = static_cast<float>(magnitudeDbAt(hz));
        // R (phaseAgreement) and weightedCoherence are two DIFFERENT
        // quantities (record §4) -- given two different curves here, not
        // the same one twice, so a reader of the specimen can tell them
        // apart the same way the real display must.
        average.phaseAgreement[k] = static_cast<float>(coherenceAt(hz));
        average.weightedCoherence[k] = static_cast<float>(coherenceAt(hz) * 0.9);
        average.phaseDeg[k] = 0.0f;  // this fixture states no phase slope
    }

    std::vector<PositionSummary> positions;
    positions.reserve(static_cast<std::size_t>(positionCount));
    for (int i = 0; i < positionCount; ++i) {
        PositionSummary summary;
        summary.tfIndex = i;
        summary.name = "POS " + std::to_string(i + 1);
        summary.levelDb = static_cast<float>(magnitudeDbAt(1000.0) - i);
        summary.weightedCoherence = static_cast<float>(coherenceAt(1000.0));
        summary.effectiveAverages = kSyntheticEffectiveAverages;
        summary.gatePassed = true;
        summary.overloaded = false;
        positions.push_back(std::move(summary));
    }

    return {std::move(average), std::move(positions)};
}

}  // namespace rta::measure
