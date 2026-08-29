// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// See docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.3, §3.4.
#pragma once

#include "measure/Snapshot.h"
#include "measure/SnapshotSource.h"

#include "rta/dsp/BandWeights.h"
#include "rta/dsp/DualFftEngine.h"
#include "rta/dsp/SpectrumEngine.h"
#include "rta/dsp/TransferEstimator.h"
#include "rta/dsp/Window.h"

#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

namespace rta::measure {

/// The pure engine-driver: turns spans of samples into a published
/// `Snapshot`, with no thread of its own. `AnalysisThread` (a later wave)
/// is the thin, JUCE-owning wrapper that calls `pushMeasurement` /
/// `publish` off a `CaptureBus` on a timer -- everything that can be tested
/// without a device or a thread lives here instead, which is the whole
/// reason this class exists as a separate testable body (plan §1.3).
///
/// Not thread-safe on the write side: `pushMeasurement`, `pushReference`,
/// `reset` and `publish` must all be called from the same thread (the
/// analysis thread, once one exists). `latest()` is the one method safe to
/// call from another thread (the message thread), because it only touches
/// the atomic snapshot pointer -- trap T-5: writer = analysis thread,
/// reader = message thread, nothing else.
class Analyser final : public SnapshotSource {
public:
    struct Config {
        std::size_t fftSize = 4096;
        std::size_t hopSize = 1024;
        double sampleRate = 48000.0;

        int fraction = 3;
        double lowHz = 20.0;
        double highHz = 20000.0;

        rta::dsp::WindowType window = rta::dsp::WindowType::Hann;
        rta::dsp::Averaging averaging = rta::dsp::Averaging::Exponential;
        double timeConstantSeconds = 0.5;

        /// Dual-FFT settings. Unused until `pushPair` is called at least once
        /// -- an RTA-only session pays for the engine's buffers and nothing
        /// else, which is cheaper than a second Analyser subclass and far
        /// cheaper than a runtime branch nobody can test.
        rta::dsp::TransferAveraging transferAveraging = rta::dsp::TransferAveraging::Fifo;
        std::size_t transferFifoDepth = 16;
        rta::dsp::Estimator estimator = rta::dsp::Estimator::H1;

        /// Positive = the measurement lags the reference by this many samples.
        /// Compensated as a stream offset before the transform; see
        /// docs/dsp/2026-08-28-dual-fft.md section 4 for why never after.
        int referenceDelaySamples = 0;
    };

    explicit Analyser(const Config& config);

    /// Feed samples for the measurement role. May throw `std::logic_error`
    /// / `std::invalid_argument` out of `SpectrumEngine::process` (trap
    /// T-3) -- this class does not swallow that; the caller (eventually
    /// `AnalysisThread::run`, inside its own try/catch) owns what happens
    /// to an exception. `Analyser` itself has no policy to be wrong about.
    void pushMeasurement(std::span<const float> samples);

    /// Feed samples for the reference role. A `Snapshot` only carries
    /// `referenceBands` (and sets `hasReference`) once this has been called
    /// at least once since construction or the last `reset()`.
    void pushReference(std::span<const float> samples);

    /// Feed one hop of BOTH channels, same time instant.
    ///
    /// Separate from pushMeasurement/pushReference, not a replacement for
    /// them: those two exist for the single-channel RTA path and impose no
    /// pairing, while a dual-FFT's one refusable precondition is that its two
    /// spans ARE the same instant. Passing the channels in separately and
    /// hoping they stay in step is the defect AnalysisThread's paired drain
    /// (task 6) exists to close.
    ///
    /// Throws `std::invalid_argument` (out of DualFftEngine::process) if the
    /// two spans differ in length.
    void pushPair(std::span<const float> reference, std::span<const float> measurement);

    /// Discards both engines' buffered samples and running averages. Does
    /// NOT reset the publish sequence counter or the last-published
    /// snapshot: those describe what has been handed to readers, which
    /// `reset()` (a mid-run device change, say) does not undo.
    void reset() noexcept;

    /// Builds a fresh, immutable `Snapshot` from everything analysed since
    /// construction/reset, publishes it via an atomic pointer swap (record:
    /// "one struct, published by atomic pointer swap"), and returns it.
    /// `droppedSamples` is carried straight into the snapshot -- it is the
    /// caller's count (typically `CaptureBus::totalDrops()`), not something
    /// this class can know on its own.
    SnapshotPtr publish(std::uint64_t droppedSamples);

    [[nodiscard]] std::uint64_t framesAnalysed() const noexcept;

    /// The most recently published snapshot, or `nullptr` before the first
    /// `publish()`. Safe to call from any thread (see class comment).
    [[nodiscard]] SnapshotPtr latest() const override;

private:
    Config config_;
    rta::dsp::BandWeights weights_;
    rta::dsp::SpectrumEngine measurementEngine_;
    rta::dsp::SpectrumEngine referenceEngine_;
    std::vector<float> bandPowerScratch_;
    std::vector<float> referencePowerScratch_;
    bool referencePushed_ = false;

    rta::dsp::DualFftEngine dual_;
    /// False until the first pushPair. Distinct from `dual_.frameCount() > 0`:
    /// a caller can push a pair shorter than one frame, and "a reference was
    /// offered" is a different fact from "a frame was analysed". Both are
    /// required before a transfer function is published.
    bool dualEngaged_ = false;

    std::uint64_t sequence_ = 0;
    std::atomic<SnapshotPtr> latest_;
};

}  // namespace rta::measure
