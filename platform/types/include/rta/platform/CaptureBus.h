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
class CaptureBus {
public:
    CaptureBus() = default;

    CaptureBus(const CaptureBus&) = delete;
    CaptureBus& operator=(const CaptureBus&) = delete;
    CaptureBus(CaptureBus&&) = delete;
    CaptureBus& operator=(CaptureBus&&) = delete;

    /// Message-thread call, and ONLY the message thread: the precondition is
    /// that the audio callback is not running concurrently with this call
    /// (device stopped, or not yet started). Rebuilds every ring at
    /// `ringCapacity` -- a fresh `RingBuffer` starts empty, which is the drain
    /// the decision record requires on every device reconfiguration, because
    /// stale samples from the previous session would otherwise splice onto
    /// the new one and mislabel every bin-to-Hz conversion. Bumps `epoch()`
    /// so a consumer thread can notice and rebuild its own analysis state.
    ///
    /// `ringCapacity` is the caller's responsibility (see plan §1.4's
    /// `bit_ceil(max(8 * bufferSize, 4 * fftSize))` formula) -- this class
    /// only knows how to size rings, not what FFT size the analysis side is
    /// using.
    void prepare(double sampleRate, int numChannels, std::size_t ringCapacity);

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
    /// Writes are additionally clipped to the number of rings this bus was
    /// `prepare()`d with: a device that suddenly reports more input channels
    /// than expected must not walk off the end of `rings_`.
    ///
    /// A full ring is a dropped block, not a partial write --
    /// `RingBuffer::write` is documented all-or-nothing, so the whole block's
    /// sample count is added to that channel's drop counter.
    void pushFromCallback(const float* const* input, int numChannels, int numSamples) noexcept;

    /// Consumer (analysis thread) access to one channel's ring. `nullptr` if
    /// `channel` is outside what this bus was last `prepare()`d for.
    [[nodiscard]] rta::dsp::RingBuffer<float>* ring(int channel) noexcept {
        if (channel < 0 || static_cast<std::size_t>(channel) >= rings_.size()) {
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
    // std::vector<RingBuffer<float>> cannot grow (reserve/emplace_back both
    // need to relocate existing elements). One heap allocation per channel,
    // done only inside prepare() -- never in pushFromCallback -- sidesteps
    // that with no change to the ring's own lock-free contract.
    std::vector<std::unique_ptr<rta::dsp::RingBuffer<float>>> rings_;
    ChannelConfig config_;
    std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(kMaxChannels)> drops_{};
    std::atomic<bool> active_{false};
    std::atomic<std::uint64_t> epoch_{0};
    std::atomic<double> sampleRate_{0.0};
    std::atomic<int> preparedChannels_{0};
};

}  // namespace rta::platform
