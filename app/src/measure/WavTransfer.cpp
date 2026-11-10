// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/WavTransfer.h"

#include "rta/dsp/AverageCount.h"
#include "rta/dsp/DelayPolicy.h"
#include "rta/dsp/DualFftEngine.h"
#include "rta/dsp/TransferEstimator.h"
#include "rta/dsp/Window.h"
#include "trace/CaptureConverter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace rta::measure {

namespace {

// Record D3: 2.93 Hz bins at 48 kHz, 75 % overlap. The same N the GATED traces
// already flow at, so EqTraceGrid / the TRANSFER overlay need nothing new.
constexpr std::size_t kFftSize = 16384;
constexpr std::size_t kHopSize = 4096;
constexpr std::int64_t kReadBlock = 65536;

// Record D4: a delay window is at most 2^18 samples (5.5 s at 48 kHz, a 2^19
// zero-padded correlation), and the plausible lag is +-1 s -- a stage-to-mic
// path is milliseconds, a console-to-recorder offset is at most a second.
constexpr std::int64_t kDelayWindow = std::int64_t{1} << 18;
constexpr double kMaxLagSeconds = 1.0;
constexpr int kMaxEngineDelay = 1 << 20;  // DualFftEngine's own cap

// Record D4: one sample, because a one-sample spread already costs -2.6 dB at
// 20 kHz / 48 kHz (sinc(0.417) = 0.738, record sec.3).
constexpr int kMaxDelaySpread = 1;

std::int64_t nowUnixMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// Frames the engine produces from `rangeLength` samples once `discard` samples
/// of the lagging stream are thrown away (DualFftEngine::armDelaySkip): the two
/// rings only pair up over the samples BOTH streams still hold.
std::size_t framesAfterDiscard(std::int64_t rangeLength, std::int64_t discard) {
    const std::int64_t aligned = rangeLength - discard;
    if (aligned < static_cast<std::int64_t>(kFftSize)) return 0;
    return static_cast<std::size_t>((aligned - static_cast<std::int64_t>(kFftSize)) / kHopSize) + 1;
}

struct Context {
    rta::dsp::Window window{rta::dsp::WindowType::Hann, kFftSize};
    double minimumAverages = rta::dsp::DualFftEngine::Config{}.minimumEffectiveAverages;
    std::int64_t rangeStart = 0;
    std::int64_t rangeLength = 0;

    [[nodiscard]] double averagesAfterDiscard(std::int64_t discard) const {
        const std::size_t frames = framesAfterDiscard(rangeLength, discard);
        if (frames == 0) return 0.0;
        return rta::dsp::fifoEffectiveAverages(window.coefficients(), kHopSize, frames);
    }
};

WavTransferOutcome refuse(WavTransferOutcome base, WavTransferRefusal why) {
    base.refusal = why;
    return base;
}

/// Window starts (relative to the range start): start / middle / end, a window
/// kept only if it does not overlap one already kept -- on a short range the
/// three collapse to two or one, because overlapping windows are not
/// independent looks at the delay.
std::vector<std::int64_t> delayWindowOffsets(std::int64_t rangeLength, std::int64_t window) {
    if (rangeLength <= window) return {0};
    const std::int64_t last = rangeLength - window;
    std::vector<std::int64_t> kept;
    for (const std::int64_t offset : {std::int64_t{0}, last / 2, last}) {
        if (kept.empty() || offset >= kept.back() + window) kept.push_back(offset);
    }
    return kept;
}

struct DelayLook {
    rta::dsp::DelaySuggestion suggestion;
    bool accepted = false;
};

}  // namespace

const char* wavTransferTrustWord(double trust) noexcept {
    return trust >= 0.5 ? "good" : (trust >= 0.15 ? "fair" : "poor");
}

WavTransferOutcome analyseWavTransfer(const SampleBlockSource& source,
                                      const WavTransferRequest& request,
                                      const std::atomic<bool>& cancel) {
    WavTransferOutcome out;
    out.referenceChannel = request.referenceChannel;
    out.measurementChannel = request.measurementChannel;
    out.fileChannels = source.numChannels();
    out.sampleRate = source.sampleRate();
    out.requestedStartSeconds = request.startSeconds;
    out.requestedLengthSeconds = request.lengthSeconds;

    // --- request validation, cheapest first ---------------------------------
    if (out.fileChannels < 2) return refuse(out, WavTransferRefusal::NotEnoughChannels);
    if (request.referenceChannel == request.measurementChannel) {
        return refuse(out, WavTransferRefusal::SameChannel);
    }
    const auto inFile = [&](int ch) { return ch >= 0 && ch < out.fileChannels; };
    if (!inFile(request.referenceChannel) || !inFile(request.measurementChannel)) {
        return refuse(out, WavTransferRefusal::ChannelOutOfRange);
    }
    const double fs = out.sampleRate;
    if (!(fs > 0.0) || !std::isfinite(fs)) return refuse(out, WavTransferRefusal::BadSampleRate);

    const std::int64_t fileLength = source.lengthSamples();
    out.fileLengthSeconds = static_cast<double>(fileLength) / fs;
    if (!(request.startSeconds >= 0.0) || !(request.lengthSeconds >= 0.0)) {
        return refuse(out, WavTransferRefusal::RangeOutsideFile);
    }
    Context ctx;
    ctx.rangeStart = std::llround(request.startSeconds * fs);
    ctx.rangeLength = request.lengthSeconds > 0.0 ? std::llround(request.lengthSeconds * fs)
                                                  : fileLength - ctx.rangeStart;
    if (ctx.rangeStart >= fileLength || ctx.rangeLength <= 0 ||
        ctx.rangeStart + ctx.rangeLength > fileLength) {
        return refuse(out, WavTransferRefusal::RangeOutsideFile);
    }

    // D5 pre-check at D = 0: if even an undelayed range cannot reach the gate,
    // reading it to find a delay is wasted work.
    out.neededAverages = ctx.minimumAverages;
    out.reachableAverages = ctx.averagesAfterDiscard(0);
    if (out.reachableAverages < ctx.minimumAverages) return refuse(out, WavTransferRefusal::RangeTooShort);

    if (cancel.load()) return refuse(out, WavTransferRefusal::Cancelled);

    // --- delay (D4) ----------------------------------------------------------
    const std::int64_t window = std::min(kDelayWindow, ctx.rangeLength);
    const auto offsets = delayWindowOffsets(ctx.rangeLength, window);
    rta::dsp::PhatOptions phat;
    phat.sampleRate = fs;
    rta::dsp::DelayPolicy policy;
    const auto maxLag = static_cast<std::ptrdiff_t>(
        std::min<std::int64_t>(std::llround(kMaxLagSeconds * fs), kMaxEngineDelay));
    policy.minLag = -maxLag;
    policy.maxLag = maxLag;

    std::vector<float> ref(static_cast<std::size_t>(window));
    std::vector<float> meas(ref.size());
    std::vector<DelayLook> looks;
    for (const std::int64_t offset : offsets) {
        if (cancel.load()) return refuse(out, WavTransferRefusal::Cancelled);
        const std::int64_t at = ctx.rangeStart + offset;
        if (!source.read(request.referenceChannel, at, ref) ||
            !source.read(request.measurementChannel, at, meas)) {
            return refuse(out, WavTransferRefusal::ReadFailed);
        }
        DelayLook look;
        look.suggestion = rta::dsp::suggestDelay(ref, meas, phat, policy);
        look.accepted = look.suggestion.verdict == rta::dsp::DelayVerdict::Accepted;
        looks.push_back(std::move(look));
    }

    const DelayLook* chosen = nullptr;
    const DelayLook* bestAny = &looks.front();
    for (const auto& look : looks) {
        if (look.suggestion.trust > bestAny->suggestion.trust) bestAny = &look;
        if (look.accepted && (chosen == nullptr || look.suggestion.trust > chosen->suggestion.trust)) {
            chosen = &look;
        }
    }
    if (chosen == nullptr) {
        out.bestTrust = bestAny->suggestion.trust;
        out.trustFloor = policy.acceptMultiple * bestAny->suggestion.nullFloor;
        return refuse(out, WavTransferRefusal::NoCorrelation);
    }
    // Any two accepted windows that disagree by more than one sample, in window
    // order, are the two the sentence names.
    for (std::size_t i = 0; i < looks.size(); ++i) {
        for (std::size_t j = i + 1; j < looks.size(); ++j) {
            if (!looks[i].accepted || !looks[j].accepted) continue;
            const auto a = looks[i].suggestion.best.delaySamples;
            const auto b = looks[j].suggestion.best.delaySamples;
            if (std::abs(a - b) > kMaxDelaySpread) {
                out.delayFirst = static_cast<int>(a);
                out.delayLast = static_cast<int>(b);
                return refuse(out, WavTransferRefusal::DelayMoved);
            }
        }
    }
    const int delay = static_cast<int>(chosen->suggestion.best.delaySamples);
    out.delaySamples = delay;
    out.trust = chosen->suggestion.trust;

    // D5 again, now that the discard is known.
    out.discardedSamples = std::abs(delay);
    out.reachableAverages = ctx.averagesAfterDiscard(out.discardedSamples);
    if (out.reachableAverages < ctx.minimumAverages) return refuse(out, WavTransferRefusal::RangeTooShort);

    // --- the average (D3) ----------------------------------------------------
    rta::dsp::DualFftEngine::Config config;
    config.fftSize = kFftSize;
    config.hopSize = kHopSize;
    config.sampleRate = fs;
    config.window = rta::dsp::WindowType::Hann;
    config.averaging = rta::dsp::TransferAveraging::Cumulative;
    config.referenceDelaySamples = delay;
    rta::dsp::DualFftEngine engine(config);

    ref.resize(static_cast<std::size_t>(kReadBlock));
    meas.resize(ref.size());
    for (std::int64_t done = 0; done < ctx.rangeLength; done += kReadBlock) {
        if (cancel.load()) return refuse(out, WavTransferRefusal::Cancelled);
        const auto n = static_cast<std::size_t>(std::min(kReadBlock, ctx.rangeLength - done));
        const std::span<float> refBlock(ref.data(), n);
        const std::span<float> measBlock(meas.data(), n);
        const std::int64_t at = ctx.rangeStart + done;
        // The SAME sample positions in both channels; the engine itself drops
        // the lagging stream's first |D| samples.
        if (!source.read(request.referenceChannel, at, refBlock) ||
            !source.read(request.measurementChannel, at, measBlock)) {
            return refuse(out, WavTransferRefusal::ReadFailed);
        }
        engine.process(refBlock, measBlock);
    }

    out.frames = engine.frameCount();
    out.effectiveAverages = engine.effectiveAverages();
    auto snapshot = rta::dsp::makeSnapshot(engine, rta::dsp::Estimator::H1);
    if (!snapshot.coherence.has_value()) {
        // The pre-check says this cannot happen; if it does, the gate wins.
        out.reachableAverages = out.effectiveAverages;
        return refuse(out, WavTransferRefusal::RangeTooShort);
    }

    // --- the trace (D6) ------------------------------------------------------
    rta::trace::CaptureMeta meta;
    meta.id = rta::trace::nextCaptureId();
    meta.capturedAtUnixMs = nowUnixMs();
    const std::string refName = "ch" + std::to_string(request.referenceChannel + 1);
    const std::string measName = "ch" + std::to_string(request.measurementChannel + 1);
    meta.deviceName = "WAV " + request.fileName + " " + measName + " vs " + refName;
    meta.channelRoles = refName + "=Reference," + measName + "=Measurement";
    meta.sampleRate = fs;
    meta.fftSize = static_cast<int>(kFftSize);
    meta.window = std::string(rta::dsp::toString(config.window));
    meta.averagingType = "Cumulative";
    meta.averagingDepth = static_cast<int>(out.frames);
    meta.effectiveAverages = out.effectiveAverages;
    meta.appliedDelaySamples = delay;

    auto trace = rta::trace::Trace::make(meta, std::move(snapshot.magnitudeDb));
    if (!trace.has_value() || !trace->setPhase(std::move(snapshot.phaseRadians)) ||
        !trace->setCoherence(std::move(*snapshot.coherence))) {
        // Unreachable: the three vectors are all numBins long. Named rather than
        // silently dropped, and ReadFailed is the nearest honest word.
        return refuse(out, WavTransferRefusal::ReadFailed);
    }
    out.trace = std::move(trace);
    return out;
}

}  // namespace rta::measure
