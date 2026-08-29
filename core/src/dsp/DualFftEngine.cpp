// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/DualFftEngine.h"

#include "rta/dsp/AverageCount.h"
#include "rta/dsp/PsdScaling.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace rta::dsp {

namespace {

/// Same generous headroom SpectrumEngine's own accumulator uses (fftSize*4,
/// floored at 4096), plus room for whichever channel keeps filling while the
/// other is skipping the leading |referenceDelaySamples| samples that align
/// the two streams (implementation note 1). Without the extra margin a large
/// delay could fill the un-skipped ring before the skip finishes and force a
/// spurious overrun.
std::size_t bufferCapacity(std::size_t fftSize, std::size_t skipMagnitude) {
    return std::bit_ceil(std::max<std::size_t>(fftSize * 4 + skipMagnitude, 4096));
}

/// Past this, a reference delay is a misconfiguration, not a measurement --
/// alignment offsets in this tool are microseconds to tens of milliseconds,
/// not tens of seconds (~21.8 s at 48 kHz), and the ring must hold the whole
/// offset in memory (bufferCapacity() folds it straight in), so an unbounded
/// value grows that allocation unboundedly. Checked HERE, in skipMagnitude(),
/// rather than only in validate(): this function runs in the constructor's
/// member-initializer list, before validate()'s body ever executes, and
/// std::abs() of an unchecked int could be INT_MIN -- negating INT_MIN is
/// undefined behaviour. The bound is checked before std::abs() is ever
/// called, so that case can no longer reach it.
constexpr int kMaxReferenceDelaySamples = 1 << 20;

std::size_t skipMagnitude(const DualFftEngine::Config& config) {
    if (config.referenceDelaySamples < -kMaxReferenceDelaySamples ||
        config.referenceDelaySamples > kMaxReferenceDelaySamples) {
        throw std::invalid_argument(
            "DualFftEngine referenceDelaySamples magnitude exceeds 1<<20 samples");
    }
    return static_cast<std::size_t>(std::abs(config.referenceDelaySamples));
}

}  // namespace

DualFftEngine::DualFftEngine(const Config& config)
    : config_(config),
      window_(config.window, config.fftSize),
      fft_(config.fftSize),
      referenceRing_(bufferCapacity(config.fftSize, skipMagnitude(config))),
      measurementRing_(bufferCapacity(config.fftSize, skipMagnitude(config))) {
    validate();

    numBins_ = fft_.numBins();

    // Shared with SpectrumEngine (PsdScaling.h) so the two engines can never
    // silently diverge into two different dB offsets for the same signal.
    densityScale_ = PsdScaling::scale(config_.sampleRate, window_.sumSquares());

    // One frame advances the clock by hopSize samples -- see SpectrumEngine's
    // identical reasoning for why the interval is the hop, not the frame.
    const double hopSeconds = static_cast<double>(config_.hopSize) / config_.sampleRate;
    alpha_ = 1.0 - std::exp(-hopSeconds / config_.timeConstantSeconds);

    referenceFrame_.resize(config_.fftSize);
    measurementFrame_.resize(config_.fftSize);
    referenceWindowed_.resize(config_.fftSize);
    measurementWindowed_.resize(config_.fftSize);
    referenceBins_.resize(numBins_);
    measurementBins_.resize(numBins_);

    fifoSxx_.assign(config_.fifoDepth * numBins_, 0.0);
    fifoSyy_.assign(config_.fifoDepth * numBins_, 0.0);
    fifoSxy_.assign(config_.fifoDepth * numBins_, std::complex<double>{0.0, 0.0});
    runningSumSxx_.assign(numBins_, 0.0);
    runningSumSyy_.assign(numBins_, 0.0);
    runningSumSxy_.assign(numBins_, std::complex<double>{0.0, 0.0});
    sxxMean_.assign(numBins_, 0.0);
    syyMean_.assign(numBins_, 0.0);
    sxyMean_.assign(numBins_, std::complex<double>{0.0, 0.0});

    armDelaySkip();
}

void DualFftEngine::validate() const {
    if (config_.hopSize == 0 || config_.hopSize > config_.fftSize) {
        throw std::invalid_argument("DualFftEngine hopSize must be within 1..fftSize");
    }
    if (!(config_.sampleRate > 0.0)) {
        throw std::invalid_argument("DualFftEngine sampleRate must be positive");
    }
    if (config_.averaging == TransferAveraging::Fifo && config_.fifoDepth == 0) {
        throw std::invalid_argument("DualFftEngine fifoDepth must be at least 1");
    }
    if (config_.averaging == TransferAveraging::Exponential &&
        !(config_.timeConstantSeconds > 0.0)) {
        throw std::invalid_argument("DualFftEngine timeConstantSeconds must be positive");
    }
    // decision record (docs/dsp/2026-08-28-dual-fft.md) §3: for a single
    // frame, |X*Y|^2 == |X|^2|Y|^2 identically, so coherence is exactly 1.0 at
    // every bin -- a completely broken engine looks flawless. The THRESHOLD
    // is a judgement (the record says so explicitly), but that SOME floor
    // must exist is not, and 1.0 averages is the floor below which the
    // quantity stops being merely noisy and becomes definitionally 1.0. This
    // check belongs on Config, not on the snapshot builder, because Config is
    // where a caller could otherwise defeat the floor entirely.
    if (!(config_.minimumEffectiveAverages >= 1.0)) {
        throw std::invalid_argument("DualFftEngine minimumEffectiveAverages must be at least 1.0");
    }
}

void DualFftEngine::armDelaySkip() noexcept {
    // Implementation note 1: positive D means the measurement lags, so the
    // leading D samples of the MEASUREMENT stream are discarded, never
    // written to its ring; negative D discards from the REFERENCE instead.
    // Exactly one of the two is ever non-zero.
    referenceSkip_ = config_.referenceDelaySamples < 0
                          ? static_cast<std::size_t>(-config_.referenceDelaySamples)
                          : 0;
    measurementSkip_ = config_.referenceDelaySamples > 0
                            ? static_cast<std::size_t>(config_.referenceDelaySamples)
                            : 0;
}

double DualFftEngine::binWidthHz() const noexcept {
    return config_.sampleRate / static_cast<double>(config_.fftSize);
}

double DualFftEngine::binFrequency(const std::size_t bin) const noexcept {
    return static_cast<double>(bin) * binWidthHz();
}

double DualFftEngine::effectiveAverages() const noexcept {
    if (config_.averaging == TransferAveraging::Fifo) {
        // K equally-weighted frames means the last fifoDepth of them -- older
        // ones have already been subtracted back out of the running sum.
        return fifoEffectiveAverages(window_.coefficients(), config_.hopSize,
                                      std::min(frameCount_, config_.fifoDepth));
    }
    return exponentialEffectiveAverages(window_.coefficients(), config_.hopSize, alpha_,
                                         frameCount_);
}

void DualFftEngine::process(std::span<const float> reference, std::span<const float> measurement) {
    if (reference.size() != measurement.size()) {
        throw std::invalid_argument(
            "DualFftEngine::process: reference and measurement must be the same length -- "
            "they are the same time instant on two channels.");
    }

    std::size_t offset = 0;
    const std::size_t total = reference.size();

    while (offset < total) {
        // The skip counters are consumed HERE and only here (implementation
        // note 1): once both reach zero the rings stay index-aligned for the
        // rest of the object's life, so there is no per-frame arithmetic
        // below that could get the alignment wrong.
        if (measurementSkip_ > 0) {
            const std::size_t room = referenceRing_.availableToWrite();
            if (room == 0) {
                drainFrames();
                if (referenceRing_.availableToWrite() == 0) {
                    throw std::logic_error("DualFftEngine accumulator cannot drain");
                }
                continue;
            }
            const std::size_t n = std::min({measurementSkip_, total - offset, room});
            referenceRing_.write(reference.subspan(offset, n));
            measurementSkip_ -= n;
            offset += n;
            continue;
        }
        if (referenceSkip_ > 0) {
            const std::size_t room = measurementRing_.availableToWrite();
            if (room == 0) {
                drainFrames();
                if (measurementRing_.availableToWrite() == 0) {
                    throw std::logic_error("DualFftEngine accumulator cannot drain");
                }
                continue;
            }
            const std::size_t n = std::min({referenceSkip_, total - offset, room});
            measurementRing_.write(measurement.subspan(offset, n));
            referenceSkip_ -= n;
            offset += n;
            continue;
        }

        const std::size_t room =
            std::min(referenceRing_.availableToWrite(), measurementRing_.availableToWrite());
        if (room == 0) {
            drainFrames();
            if (std::min(referenceRing_.availableToWrite(), measurementRing_.availableToWrite()) ==
                0) {
                throw std::logic_error("DualFftEngine accumulator cannot drain");
            }
            continue;
        }
        const std::size_t n = std::min(room, total - offset);
        referenceRing_.write(reference.subspan(offset, n));
        measurementRing_.write(measurement.subspan(offset, n));
        offset += n;
        drainFrames();
    }
}

void DualFftEngine::drainFrames() {
    // Pairing only happens once BOTH rings hold a full frame -- this is the
    // "both rings have a frame -> take one from each, discard hop from each"
    // rule implementation note 1 describes. The skip phase above is what
    // makes that rule sufficient on its own.
    while (referenceRing_.availableToRead() >= config_.fftSize &&
           measurementRing_.availableToRead() >= config_.fftSize) {
        const std::size_t gotRef = referenceRing_.peek(referenceFrame_);
        const std::size_t gotMeas = measurementRing_.peek(measurementFrame_);
        if (gotRef != config_.fftSize || gotMeas != config_.fftSize) {
            throw std::logic_error("DualFftEngine read a short frame pair");
        }
        referenceRing_.discard(config_.hopSize);
        measurementRing_.discard(config_.hopSize);
        analyseFramePair();
    }
}

void DualFftEngine::analyseFramePair() {
    window_.apply(referenceFrame_, referenceWindowed_);
    window_.apply(measurementFrame_, measurementWindowed_);
    fft_.forward(referenceWindowed_, referenceBins_);
    fft_.forward(measurementWindowed_, measurementBins_);

    ++frameCount_;

    for (std::size_t k = 0; k < numBins_; ++k) {
        // Promote to double BEFORE any arithmetic: Sxy = conj(X)*Y and Sxx =
        // |X|^2 must be computed from the SAME double-precision X, Y so that
        // "y == x" identically produces a bit-exact real Sxy == Sxx -- see the
        // "channel measured against itself" test, which asserts that to 1e-18.
        const std::complex<double> x(referenceBins_[k]);
        const std::complex<double> y(measurementBins_[k]);
        const double factor = densityScale_ * PsdScaling::binFactor(k, numBins_);

        const double sxx = std::norm(x) * factor;
        const double syy = std::norm(y) * factor;
        // Sxy = conj(X) * Y, matching scipy.signal.csd(x, y) -- see the naming
        // table in docs/plans/2026-08-29-L2-dual-fft-impl-plan.md. X*conj(Y)
        // would flip the sign of the phase and of the delay convention.
        const std::complex<double> sxy = std::conj(x) * y * factor;

        accumulate(k, sxx, syy, sxy);
    }
}

void DualFftEngine::accumulate(std::size_t bin, double sxx, double syy,
                                std::complex<double> sxy) noexcept {
    if (config_.averaging == TransferAveraging::Fifo) {
        const std::size_t depth = config_.fifoDepth;
        const std::size_t slot = (frameCount_ - 1) % depth;
        if (frameCount_ > depth) {
            // The frame about to be overwritten must be un-summed FIRST -- an
            // exact running sum, not a smoothed one, which is what "the FIFO
            // forgets exactly at its depth" (record §5) requires.
            runningSumSxx_[bin] -= fifoSxx_[slot * numBins_ + bin];
            runningSumSyy_[bin] -= fifoSyy_[slot * numBins_ + bin];
            runningSumSxy_[bin] -= fifoSxy_[slot * numBins_ + bin];
        }
        fifoSxx_[slot * numBins_ + bin] = sxx;
        fifoSyy_[slot * numBins_ + bin] = syy;
        fifoSxy_[slot * numBins_ + bin] = sxy;
        runningSumSxx_[bin] += sxx;
        runningSumSyy_[bin] += syy;
        runningSumSxy_[bin] += sxy;

        // Accumulators are MEANS, not sums (implementation note 3), so they
        // are directly comparable with SpectrumEngine::density().
        const double count = static_cast<double>(std::min(frameCount_, depth));
        sxxMean_[bin] = runningSumSxx_[bin] / count;
        syyMean_[bin] = runningSumSyy_[bin] / count;
        sxyMean_[bin] = runningSumSxy_[bin] / count;
    } else {
        // One-pole, seeded from the first frame exactly as SpectrumEngine
        // seeds -- an average started at zero would spend several time
        // constants climbing out of a hole that is not a measurement.
        if (frameCount_ == 1) {
            sxxMean_[bin] = sxx;
            syyMean_[bin] = syy;
            sxyMean_[bin] = sxy;
        } else {
            sxxMean_[bin] += alpha_ * (sxx - sxxMean_[bin]);
            syyMean_[bin] += alpha_ * (syy - syyMean_[bin]);
            sxyMean_[bin] += alpha_ * (sxy - sxyMean_[bin]);
        }
    }
}

void DualFftEngine::reset() noexcept {
    referenceRing_.reset();
    measurementRing_.reset();

    std::fill(runningSumSxx_.begin(), runningSumSxx_.end(), 0.0);
    std::fill(runningSumSyy_.begin(), runningSumSyy_.end(), 0.0);
    std::fill(runningSumSxy_.begin(), runningSumSxy_.end(), std::complex<double>{0.0, 0.0});
    std::fill(sxxMean_.begin(), sxxMean_.end(), 0.0);
    std::fill(syyMean_.begin(), syyMean_.end(), 0.0);
    std::fill(sxyMean_.begin(), sxyMean_.end(), std::complex<double>{0.0, 0.0});
    // The FIFO slots themselves are left stale on purpose: frameCount_ == 0
    // makes accumulate() treat every slot as never having been filled, so
    // nothing ever reads them before they are overwritten again.
    frameCount_ = 0;

    // Implementation note 2: the delay is construction-only, and reset() must
    // re-arm it from the ORIGINAL config, because a skip counter left
    // mid-count from before the reset would mis-align the next stream.
    armDelaySkip();
}

}  // namespace rta::dsp
