// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// See docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.3, §3.4.
#include "measure/Analyser.h"

#include "measure/AnalyserPublish.h"
#include "measure/Levels.h"

#include "rta/dsp/OctaveBands.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace rta::measure {

namespace {

rta::dsp::SpectrumEngine::Config toEngineConfig(const Analyser::Config& config) {
    rta::dsp::SpectrumEngine::Config engineConfig;
    engineConfig.fftSize = config.fftSize;
    engineConfig.hopSize = config.hopSize;
    engineConfig.sampleRate = config.sampleRate;
    engineConfig.window = config.window;
    engineConfig.averaging = config.averaging;
    engineConfig.timeConstantSeconds = config.timeConstantSeconds;
    return engineConfig;
}

rta::dsp::DualFftEngine::Config toDualConfig(const Analyser::Config& config) {
    rta::dsp::DualFftEngine::Config dualConfig;
    dualConfig.fftSize = config.fftSize;
    dualConfig.hopSize = config.hopSize;
    dualConfig.sampleRate = config.sampleRate;
    dualConfig.window = config.window;
    dualConfig.averaging = config.transferAveraging;
    dualConfig.fifoDepth = config.transferFifoDepth;
    // TransferAveraging::Exponential needs this too, same as
    // toEngineConfig() -- without it, Exponential is a mode Config exposes
    // and nobody can tune.
    dualConfig.timeConstantSeconds = config.timeConstantSeconds;
    dualConfig.referenceDelaySamples = config.referenceDelaySamples;
    return dualConfig;
}

rta::dsp::MtwConfig toMtwConfig(const Analyser::Config& config) {
    rta::dsp::MtwConfig mtwConfig;
    mtwConfig.topFftSize = config.mtwTopFftSize;
    mtwConfig.octaveCount = config.mtwOctaveCount;
    mtwConfig.sampleRate = config.sampleRate;
    mtwConfig.window = config.window;
    mtwConfig.averaging = config.mtwAveraging;
    mtwConfig.fifoDepth = config.mtwFifoDepth;
    mtwConfig.timeConstantFrames = config.mtwTimeConstantFrames;
    // Every band runs at full rate (record §2), so the same integer sample
    // count compensates the delay in every band -- see MtwEngine.cpp's own
    // bandConfig() for why there is no fractional path to invent.
    mtwConfig.referenceDelaySamples = config.referenceDelaySamples;
    // minimumEffectiveAverages left at MtwConfig's own default (8.0, same as
    // DualFftEngine's) -- Analyser::Config exposes no override for the fixed
    // engine's gate either.
    return mtwConfig;
}

/// Fills `out` from one role's engine + the shared band weights, in the
/// app's own units (hertz, dBFS) -- the conversion `RtaView` and the tests
/// both read against, defined once in Levels.h.
void readBands(const rta::dsp::BandWeights& weights,
               const rta::dsp::SpectrumEngine& engine,
               std::span<float> powerScratch,
               std::vector<BandReading>& out) {
    // Trap T-2: BandWeights::apply consumes density(), NEVER spectrum().
    // Summing the power spectrum over a band over-counts broadband energy by
    // the window's equivalent noise bandwidth -- 1.76 dB high with Hann, on
    // every noise or music source, on a plot that looks entirely reasonable.
    // See SpectrumEngine.h and docs/dsp/2026-08-26-banding-and-averaging.md.
    weights.apply(engine.density(), powerScratch);

    out.resize(weights.size());
    for (std::size_t i = 0; i < weights.size(); ++i) {
        const auto& band = weights.bands()[i];
        BandReading& reading = out[i];
        reading.centreHz = static_cast<float>(band.centre);
        reading.lowerHz = static_cast<float>(band.lower);
        reading.upperHz = static_cast<float>(band.upper);
        reading.levelDb = static_cast<float>(levelDbFs(static_cast<double>(powerScratch[i])));
        reading.underResolved = weights.band(i).underResolved;
    }
}

}  // namespace

Analyser::Analyser(const Config& config)
    : config_(config)
    , weights_(rta::dsp::OctaveBands(config.fraction, config.lowHz, config.highHz),
               config.fftSize, config.sampleRate)
    , measurementEngine_(toEngineConfig(config))
    , referenceEngine_(toEngineConfig(config))
    , bandPowerScratch_(weights_.size(), 0.0f)
    , referencePowerScratch_(weights_.size(), 0.0f)
    // DualFftEngine has no default constructor, so it must be built here from
    // Config rather than assigned later -- see the Config field comments for
    // why an RTA-only session still pays for these buffers.
    , dual_(toDualConfig(config))
    // Same reasoning as dual_ -- MtwEngine has no default constructor either,
    // and is built unconditionally regardless of mtwEnabled (Analyser.h's own
    // comment on mtw_).
    , mtw_(toMtwConfig(config)) {}
    // latest_ default-constructs to an empty AtomicSharedPtr, i.e. latest()
    // returns nullptr until the first publish().

void Analyser::pushMeasurement(std::span<const float> samples) {
    measurementEngine_.process(samples);
}

void Analyser::pushReference(std::span<const float> samples) {
    referenceEngine_.process(samples);
    referencePushed_ = true;
}

void Analyser::pushPair(std::span<const float> reference, std::span<const float> measurement) {
    // Validated HERE, before touching any engine -- not left to
    // DualFftEngine::process, whose own check runs only after the two
    // SpectrumEngine::process calls below. Refusing the pair AFTER already
    // feeding those would leave a half-accepted call behind: bands advanced,
    // hasReference latched, from a call whose contract says it never
    // happened.
    if (reference.size() != measurement.size()) {
        throw std::invalid_argument(
            "Analyser::pushPair: reference and measurement spans must be the same length");
    }

    // Feed the single-channel RTA path too, so a paired push still lights up
    // the band/spectrum readouts -- pushPair is additional to
    // pushMeasurement/pushReference, not a substitute for what they publish.
    referenceEngine_.process(reference);
    referencePushed_ = true;
    measurementEngine_.process(measurement);

    dual_.process(reference, measurement);
    dualEngaged_ = true;

    // A second engine fed the SAME pair, not a decimated or resampled one
    // (record §2's "no decimation" decision) -- mtwEnabled gates whether this
    // runs at all, so a caller who wants no MTW cost pays none per hop.
    if (config_.mtwEnabled) {
        mtw_.process(reference, measurement);
        mtwEngaged_ = true;
    }
}

void Analyser::reset() noexcept {
    measurementEngine_.reset();
    referenceEngine_.reset();
    referencePushed_ = false;
    dual_.reset();
    dualEngaged_ = false;
    mtw_.reset();
    mtwEngaged_ = false;
}

SnapshotPtr Analyser::publish(std::uint64_t droppedSamples) {
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->sequence = ++sequence_;
    snapshot->sampleRate = config_.sampleRate;
    snapshot->fftSize = config_.fftSize;
    snapshot->fraction = config_.fraction;
    snapshot->framesAnalysed = measurementEngine_.frameCount();
    snapshot->droppedSamples = droppedSamples;

    readBands(weights_, measurementEngine_, bandPowerScratch_, snapshot->bands);

    float peakDb = static_cast<float>(kLevelFloorDb);
    float peakHz = 0.0f;
    for (const auto& band : snapshot->bands) {
        if (band.levelDb > peakDb) {
            peakDb = band.levelDb;
            peakHz = band.centreHz;
        }
    }
    snapshot->peakBandLevelDb = peakDb;
    snapshot->peakBandCentreHz = peakHz;

    // The raw per-bin trace: SpectrumEngine::spectrum() is the right getter
    // here (not density()) -- this is a per-bin reading meant to show a
    // discrete tone at its true level, the opposite case from the banded
    // sum readBands() computes. See SpectrumEngine.h's "two outputs are not
    // interchangeable" note.
    const auto spectrum = measurementEngine_.spectrum();
    snapshot->spectrumDb.resize(spectrum.size());
    for (std::size_t k = 0; k < spectrum.size(); ++k) {
        snapshot->spectrumDb[k] =
            static_cast<float>(levelDbFs(static_cast<double>(spectrum[k])));
    }

    snapshot->hasReference = referencePushed_;
    if (referencePushed_) {
        readBands(weights_, referenceEngine_, referencePowerScratch_, snapshot->referenceBands);
    }

    if (dualEngaged_ && dual_.frameCount() > 0) {
        const auto tf = rta::dsp::makeSnapshot(dual_, config_.estimator);
        snapshot->transfer = makeTransferBlock(tf, config_.referenceDelaySamples);
    }

    if (mtwEngaged_) {
        // The ONE place an MtwResult is stitched from bands, mirroring the
        // fixed engine's own makeSnapshot() call three lines above. The block
        // builder itself -- including the one place a flat per-point
        // coherence array is written from the per-band gated snapshots
        // (record §5's guard note) -- lives in AnalyserPublish.cpp, outside
        // check_coherence_gate.cmake's reach, after the gate already ran
        // inside makeMtwResult().
        const auto mtwResult = rta::dsp::makeMtwResult(mtw_, config_.estimator);
        snapshot->mtw = makeMtwBlock(mtwResult, config_.referenceDelaySamples);
    }

    SnapshotPtr result(snapshot);
    // Trap T-5 / decision record: one struct, published by an atomic
    // pointer swap. Writer = analysis thread (this call), reader = message
    // thread via latest() -- neither side is the audio callback.
    latest_.store(result, std::memory_order_release);
    return result;
}

std::uint64_t Analyser::framesAnalysed() const noexcept {
    return measurementEngine_.frameCount();
}

SnapshotPtr Analyser::latest() const {
    return latest_.load(std::memory_order_acquire);
}

}  // namespace rta::measure
