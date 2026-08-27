// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_platform_types. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/dsp/RingBuffer.h"
#include "rta/platform/ChannelConfig.h"

#include <array>
#include <atomic>
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
/// The fix is to make reallocation impossible rather than to guard it: this
/// class allocates all `kMaxChannels` rings ONCE, in the constructor, at
/// `kFixedRingCapacitySamples` (see above), and never again. `prepare()` now
/// only resets and re-labels those same, permanently-allocated objects --
/// `ring(k)` returns the identical object across any number of `prepare()`
/// calls, so a pointer an analysis thread is holding stays valid for the
/// life of the `CaptureBus`, full stop. No lock was added; the hazard is
/// closed because the thing it depended on (reallocation) no longer exists.
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

    /// Message-thread call, and ONLY the message thread: the precondition is
    /// that the audio callback is not running concurrently with this call
    /// (device stopped, or not yet started) -- unchanged from before. What
    /// changed is that this no longer needs a second precondition about
    /// consumers: it performs no allocation and never invalidates a
    /// `ring()` pointer a consumer thread already holds (see the class
    /// comment).
    ///
    /// Resets every one of the `kMaxChannels` rings -- not only the first
    /// `numChannels` -- back to empty (`RingBuffer::reset()`, index zero on
    /// both sides; the storage itself, already zero-initialised at
    /// construction, is left alone since a reader never sees past its own
    /// write index). That is the drain the decision record requires on
    /// every device reconfiguration: stale samples from the previous
    /// session would otherwise splice onto the new one and mislabel every
    /// bin-to-Hz conversion. Also clears every drop counter (a device
    /// reconfiguration invalidates whatever was dropped under the previous
    /// session too) and bumps `epoch()` so a consumer thread can notice and
    /// rebuild its own analysis state.
    void prepare(double sampleRate, int numChannels);

    /// Message thread. A bus that is not active writes nothing and drops
    /// nothing -- a block discarded because the device is stopped is not the
    /// same event as a block discarded because the consumer fell behind.
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
    [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_.load(std::memory_order_relaxed); }

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
    std::atomic<double> sampleRate_{0.0};
    std::atomic<int> preparedChannels_{0};
};

}  // namespace rta::platform
