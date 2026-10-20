// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_platform_types. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/dsp/RingBuffer.h"
#include "rta/platform/ChannelConfig.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace rta::platform {

/// Fixed per-channel ring capacity, in samples, used by every production
/// `CaptureBus` (tests are free to construct one at a smaller capacity to
/// keep fixtures cheap and exact -- see `platform/tests/test_capture_bus.cpp`).
///
/// `bit_ceil(max(8 * bufferSize, 4 * fftSize))` per the original sizing
/// formula (plan §1.4), evaluated at the worst case either input can reach
/// rather than at today's defaults: `bufferSize = 2048` (the top of the
/// buffer sizes a real interface commonly offers) and `fftSize = 16384`
/// (headroom above `Analyser::Config`'s 4096 default for a future
/// high-resolution mode) --
///
///     bit_ceil(max(8 * 2048, 4 * 16384)) = bit_ceil(max(16384, 65536)) = 65536
///
/// 64 channels x 65536 samples x 4 bytes = 16 MB, allocated once and held for
/// the life of the process. That fixed cost buys the absence of a race that
/// cannot be locked away: see the class comment for why capacity can never
/// change after construction. Sized at the worst case on purpose -- since
/// there is no second chance to grow it, undersizing here would silently
/// reintroduce drops (not a crash, just lost audio) the day either input
/// parameter grows past what was assumed when this constant was chosen.
inline constexpr std::size_t kFixedRingCapacitySamples = 65536;

/// The audio-callback-facing side of the platform layer: one lock-free ring
/// per channel, the role table, per-channel drop counters, and a validity
/// atomic. `AudioIo::audioDeviceIOCallbackWithContext` is reduced to
/// `ScopedNoDenormals` then one call into `pushFromCallback` here -- every
/// rule the decision record states about the callback body (bounds against
/// the channel count RECEIVED, count rather than swallow short writes, do
/// nothing when inactive) lives in this class so it is provable with plain
/// `const float* const*` arrays and no device.
///
/// `SyntheticInput` (app/) writes into the same bus through the same
/// `pushFromCallback` entry point the real driver uses -- one write path,
/// not two -- which is what makes "the analysis chain is drivable without
/// hardware" true rather than a second code path that drifts.
///
/// ## Fixed capacity, and why `prepare()` no longer allocates
///
/// A device change calls `prepare()` from the DEVICE thread
/// (`AudioIo::audioDeviceAboutToStart`), and `prepare()`'s own precondition
/// only promises the audio callback is quiesced -- it says nothing about
/// `AnalysisThread`, which is a live CONSUMER that may be mid-drain, holding
/// a `RingBuffer<float>*` it fetched from `ring()` moments before, on its
/// own thread, with no lock between the two. The old implementation rebuilt
/// `rings_` on every `prepare()` (`rings_.clear()` then fresh
/// `make_unique<RingBuffer<float>>` per channel): if that ran while
/// `AnalysisThread::drainRole` was still holding the OLD pointer, the
/// consumer thread would call `peek()`/`discard()` on a freed object --
/// a genuine use-after-free, not merely a stale read, and one no amount of
/// locking on the platform side can fix without also making the audio
/// callback lock-taking (which real-time safety forbids).
///
/// The fix for THAT hazard is to make reallocation impossible rather than to
/// guard it: this class allocates all `kMaxChannels` rings ONCE, in the
/// constructor, at `kFixedRingCapacitySamples` (see above), and never again.
/// `ring(k)` returns the identical object across any number of `prepare()`
/// calls, so a pointer an analysis thread is holding stays valid for the
/// life of the `CaptureBus`.
///
/// ## What `prepare()` must NOT do: touch a ring's indices
///
/// Closing reallocation did not close the index race. `RingBuffer` is SPSC:
/// the consumer alone writes `readIndex_`, and `RingBuffer::reset()` writes
/// it ("call only when both sides are known to be stopped"). `prepare()`
/// runs on the device or message thread while `AnalysisThread` is live, so a
/// `reset()` there can land between the consumer's two loads inside
/// `discard()` and leave `write=0, read=R+hop` -- `availableToRead()` then
/// wraps to ~2^64 and the analysis thread never leaves its drain loop. So
/// `prepare()` touches no ring at all. The structural guard test
/// `test_capture_bus_guard.cpp` fails the build if it ever calls `reset(`.
///
/// ## Resync: how the previous session's samples are dropped
///
/// Draining must leave every ring EMPTY at the same callback boundary, or
/// reference and measurement end up offset (a ring that entered the epoch
/// with more data keeps old-rate samples at its front; draining an equal
/// count from each ring, or each ring's own backlog while the producer
/// writes block-by-block, cannot fix that). So the drain is bracketed by a
/// producer gate:
///
///   1. `prepare()` (device stopped) -- or the consumer, on seeing the SET of
///      written channels change while running -- raises `resyncPending_`.
///   2. `pushFromCallback` loads it once (acquire); while it is up the
///      callback writes NO ring and counts the frames in `resyncFrames()`,
///      which are not overflow drops.
///   3. The consumer (`serviceResyncAsConsumer`) empties every ring with
///      `discard(availableToRead())` -- stable, nothing is writing -- and
///      only then lowers the flag. The next callback writes all routed rings
///      together, so they all restart empty on one boundary.
///
/// "Nothing is writing" is a proof, not a hope: a callback that was already
/// past its gate check when the flag rose could still be mid-block. The
/// audio thread runs callbacks one at a time, so a callback that SEES the
/// flag up proves the previous one has returned. It bumps `gateSeen_`; the
/// consumer drains only after that counter has moved since it started
/// waiting -- or at once when the bus is not active (no callbacks exist), or
/// after a bounded wait (`resyncAckTimeouts()` counts that fallback).
///
/// Everything the resync throws away is time the SPL log must not mistake
/// for continuous: `resyncLostFrames()` (frames the gate refused plus the
/// backlog the drain discarded) is added to the per-channel drop count the
/// SPL feed reports, so the affected block carries `BlockFlag::Gap`.
///
/// The gate only exists while a consumer is attached (`attachConsumer`):
/// with none there is nobody to lower it, and nobody to race.
class CaptureBus {
public:
    /// Allocates all `kMaxChannels` rings up front, each rounded up to a
    /// power of two by `RingBuffer`'s own constructor (see
    /// `rta::dsp::RingBuffer`). This is the ONLY place this class ever
    /// allocates -- after construction returns, no method here touches the
    /// heap, including `prepare()` (see the class comment for why that
    /// matters). Pass `kFixedRingCapacitySamples` in production; tests may
    /// pass a smaller capacity to keep fixtures cheap and exact.
    explicit CaptureBus(std::size_t ringCapacity);

    CaptureBus(const CaptureBus&) = delete;
    CaptureBus& operator=(const CaptureBus&) = delete;
    CaptureBus(CaptureBus&&) = delete;
    CaptureBus& operator=(CaptureBus&&) = delete;

    /// Device-thread or message-thread call; the precondition is that the
    /// AUDIO CALLBACK is not running concurrently with it (device stopped, or
    /// not yet started). It performs no allocation, never invalidates a
    /// `ring()` pointer and never touches any ring's indices (see the class
    /// comment), so it is safe against a live `AnalysisThread`.
    ///
    /// Clears every drop and resync counter, re-labels the rate and channel
    /// count, raises the resync gate if a consumer is attached, and bumps
    /// `epoch()` so the consumer rebuilds its analysis state. Samples from the
    /// previous session stay in the rings until the consumer's resync drains
    /// them; the gate keeps new-session samples out until it has.
    void prepare(double sampleRate, int numChannels);

    /// The consumer announces itself (AnalysisThread's constructor) and
    /// withdraws (its destructor, after the thread has stopped). Attached,
    /// `prepare()` raises the resync gate; detaching lowers it.
    void attachConsumer() noexcept;
    void detachConsumer() noexcept;

    /// CONSUMER THREAD ONLY, once per tick, before any read. Detects a change
    /// in the set of channels with a writer (a role assigned or cleared while
    /// running) and raises the gate for it. While the gate is up it waits for
    /// proof that no callback is mid-block (see the class comment), then
    /// empties every ring and lowers the gate (returns true). Returns false
    /// when nothing is pending or the proof has not arrived yet.
    bool serviceResyncAsConsumer() noexcept;

    /// CONSUMER THREAD ONLY: `discard(availableToRead())` on every ring;
    /// returns the largest count discarded from any one ring. Correct only
    /// while nothing is writing -- the gate is up and acknowledged, or the
    /// device is stopped. Public so tests can play the consumer.
    std::size_t drainAsConsumer() noexcept;

    /// How long the consumer waits for a callback to acknowledge the gate
    /// before draining anyway. Default 500 ms; a setter so a test need not
    /// sleep that long. Consumer thread (or before it starts).
    void setResyncAckTimeout(std::chrono::milliseconds timeout) noexcept { consumerAckTimeout_ = timeout; }

    /// True while the producer is gated off. Frames the callback refused
    /// during a resync; NOT overflow drops, and not in `totalDrops()`.
    [[nodiscard]] bool resyncPending() const noexcept { return resyncPending_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint64_t resyncFrames() const noexcept { return resyncFrames_.load(std::memory_order_relaxed); }
    /// Monotonic, never reset: frames the gate refused + backlog the drains
    /// discarded. What the SPL feed adds to a channel's drop count.
    [[nodiscard]] std::uint64_t resyncLostFrames() const noexcept { return resyncLostFrames_.load(std::memory_order_relaxed); }
    /// Monotonic; bumped when the gate rises and again when a drain completes,
    /// so a value that differs from the one read earlier means a resync began
    /// or finished in between (a sweep capture armed before it is shifted).
    [[nodiscard]] std::uint64_t resyncSequence() const noexcept { return resyncSeq_.load(std::memory_order_acquire); }
    /// Drains that went ahead on the timeout, with no callback acknowledgement.
    [[nodiscard]] std::uint64_t resyncAckTimeouts() const noexcept { return resyncAckTimeouts_.load(std::memory_order_relaxed); }

    /// Message thread OR device thread -- a relaxed atomic store, so it is
    /// safe from either, and `AudioIo` calls it from all three of its
    /// device-death paths (`stop()`, `audioDeviceStopped()`,
    /// `audioDeviceError()`), two of which are device-thread callbacks. Said
    /// explicitly because this line read "Message thread" while those callers
    /// already existed, and a reader who believes it will hesitate to arm or
    /// disarm the bus from the very thread that learns the device is gone.
    /// Not for the audio callback: that path only ever reads `isActive()`.
    ///
    /// A bus that is not active writes nothing and drops nothing -- a block
    /// discarded because the device is stopped is not the same event as a
    /// block discarded because the consumer fell behind.
    void setActive(bool active) noexcept { active_.store(active, std::memory_order_relaxed); }

    [[nodiscard]] bool isActive() const noexcept { return active_.load(std::memory_order_relaxed); }

    /// Audio-callback call. Wait-free, no allocation, no locks.
    ///
    /// `numChannels` is the count the callback ACTUALLY RECEIVED this block
    /// (decision record: never trust what the device advertised at open
    /// time). Roles are bounds-checked against that count, not against
    /// `numChannels` prepared for -- a channel role configured beyond what
    /// this block delivered is ignored for this call, per `ChannelConfig`.
    ///
    /// Writes are additionally clipped to `preparedChannels_` -- the channel
    /// count this bus was last `prepare()`d with: a device that suddenly
    /// reports more input channels than expected must not write into a
    /// channel no role table entry has claimed, even though the ring object
    /// for it physically exists (all `kMaxChannels` rings are always
    /// allocated; see the class comment).
    ///
    /// A full ring is a dropped block, not a partial write --
    /// `RingBuffer::write` is documented all-or-nothing, so the whole block's
    /// sample count is added to that channel's drop counter.
    void pushFromCallback(const float* const* input, int numChannels, int numSamples) noexcept;

    /// Consumer (analysis thread) access to one channel's ring. `nullptr` if
    /// `channel` is outside what this bus was last `prepare()`d for --
    /// checked against `preparedChannels_`, NOT against how many rings
    /// physically exist (that is always `kMaxChannels`; see the class
    /// comment). A channel this bus was never prepared with stays reported
    /// as absent even though its `RingBuffer` object is sitting there
    /// allocated, so callers cannot accidentally read a channel role has not
    /// claimed. For any `channel` this DOES return non-null for, the
    /// returned pointer is valid for the life of the `CaptureBus` -- it is
    /// never reseated by a later `prepare()`, unlike before this class
    /// closed the reallocation hazard.
    [[nodiscard]] rta::dsp::RingBuffer<float>* ring(int channel) noexcept {
        const int prepared = preparedChannels_.load(std::memory_order_relaxed);
        if (channel < 0 || channel >= prepared) {
            return nullptr;
        }
        return rings_[static_cast<std::size_t>(channel)].get();
    }

    [[nodiscard]] std::uint64_t dropCount(int channel) const noexcept {
        if (channel < 0 || channel >= kMaxChannels) {
            return 0;
        }
        return drops_[static_cast<std::size_t>(channel)].load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t totalDrops() const noexcept {
        std::uint64_t total = 0;
        const int prepared = preparedChannels_.load(std::memory_order_relaxed);
        for (int ch = 0; ch < prepared; ++ch) {
            total += drops_[static_cast<std::size_t>(ch)].load(std::memory_order_relaxed);
        }
        return total;
    }

    /// Bumped by every `prepare()` -- a device-level reconfiguration. See
    /// `ChannelConfig::configEpoch()` for the (deliberately separate) role
    /// change counter.
    [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_.load(std::memory_order_acquire); }

    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_.load(std::memory_order_relaxed); }

    [[nodiscard]] int numChannels() const noexcept { return preparedChannels_.load(std::memory_order_relaxed); }

    [[nodiscard]] ChannelConfig& config() noexcept { return config_; }
    [[nodiscard]] const ChannelConfig& config() const noexcept { return config_; }

private:
    // RingBuffer holds atomics, so it is neither copyable nor movable, and a
    // std::vector<RingBuffer<float>> cannot grow in place (reserve/
    // emplace_back both need to relocate existing elements) -- hence
    // unique_ptr per slot rather than storing RingBuffer<float> by value.
    // Sized to kMaxChannels exactly once, in the constructor, and never
    // resized again: see the class comment for why a second allocation here
    // is the thing that made a use-after-free reachable, and why the fix is
    // to make that impossible rather than to lock around it.
    std::vector<std::unique_ptr<rta::dsp::RingBuffer<float>>> rings_;
    ChannelConfig config_;
    std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(kMaxChannels)> drops_{};
    std::atomic<bool> active_{false};
    std::atomic<std::uint64_t> epoch_{0};
    std::atomic<bool> consumerAttached_{false};
    std::atomic<bool> resyncPending_{false};
    std::atomic<std::uint64_t> resyncFrames_{0};
    std::atomic<std::uint64_t> resyncLostFrames_{0};
    std::atomic<std::uint64_t> resyncSeq_{0};
    std::atomic<std::uint64_t> resyncAckTimeouts_{0};
    std::atomic<std::uint64_t> gateSeen_{0};  // callbacks that observed the gate up
    // gateSeen_ as it stood when the gate last rose: a callback that sees the
    // gate up AFTER that moves gateSeen_ past it, which is the acknowledgement.
    std::atomic<std::uint64_t> gateSeenAtRaise_{0};
    void raiseGate() noexcept;
    // Consumer thread only: the clock on the bounded wait for that.
    bool consumerWaiting_ = false;
    std::chrono::steady_clock::time_point consumerRaisedAt_{};
    std::chrono::milliseconds consumerAckTimeout_{500};
    // Consumer thread only (serviceResyncAsConsumer): bit ch set = channel ch had a writer.
    std::uint64_t consumerWriterMask_ = 0;
    [[nodiscard]] std::uint64_t writerMask() const noexcept;
    std::atomic<double> sampleRate_{0.0};
    std::atomic<int> preparedChannels_{0};
};

}  // namespace rta::platform
