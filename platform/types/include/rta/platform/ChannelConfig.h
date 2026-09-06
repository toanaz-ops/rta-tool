// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_platform_types. No JUCE, no Qt, no audio-device API.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <string_view>

namespace rta::platform {

/// What the analysis chain does with one input channel.
enum class ChannelRole {
    Unused,       ///< Not read by anything; the ring for it is never written.
    Measurement,  ///< The signal under measurement.
    Reference,    ///< The reference / send signal, for a future transfer function.
};

/// Covers a MADI or Dante interface. Fixed so the role table is a fixed-size
/// array and no allocation happens anywhere near the audio callback.
inline constexpr int kMaxChannels = 64;

[[nodiscard]] constexpr std::string_view toString(ChannelRole role) noexcept {
    switch (role) {
        case ChannelRole::Unused: return "Unused";
        case ChannelRole::Measurement: return "Measurement";
        case ChannelRole::Reference: return "Reference";
    }
    return "Unused";
}

/// The message-thread-owned table of "what is channel N for", read by the
/// audio callback on every block.
///
/// ## Why the array is atomic, not mutex-guarded
///
/// The callback must never block, so it cannot take a lock the message thread
/// might be holding while repainting the channel table. Each slot is its own
/// `std::atomic<int>` (storing a `ChannelRole`), written with a plain relaxed
/// store from the message thread and read with a relaxed snapshot from the
/// callback. Relaxed is enough because a role flip a few samples late is a
/// UI-visible glitch at worst, never a memory-safety issue -- there is no
/// second piece of data this needs to stay ordered with.
///
/// ## The bounds rule (decision record)
///
/// `snapshot()` takes the channel count the callback **actually received**
/// this block, not the count the device advertised at open time. A channel
/// index at or beyond that count is forced to `Unused` in the snapshot,
/// regardless of what was configured -- because a device that silently drops
/// to fewer channels (a common ASIO buffer-size-change artefact) must not
/// have the analysis chain read past what it handed over.
///
/// ## Two epochs, two different things
///
/// `configEpoch()` here counts role reassignments (message thread, no ring
/// drain needed -- the callback just reads fresh roles on the next block).
/// `CaptureBus::epoch()` is a different counter entirely: it counts device
/// reconfigurations (`prepare()`), which DO require draining every ring
/// because the sample rate changed. Do not conflate the two -- a role change
/// must not stall audio by draining, and a device change must not be silently
/// absorbed by a role-only counter.
class ChannelConfig {
public:
    ChannelConfig() noexcept {
        for (auto& slot : roles_) {
            slot.store(static_cast<int>(ChannelRole::Unused), std::memory_order_relaxed);
        }
        for (auto& slot : tfIndex_) {
            slot.store(0, std::memory_order_relaxed);
        }
    }

    // Holds std::atomic members: already non-copyable and non-movable by
    // default. Spelled out here so the intent reads at the declaration.
    ChannelConfig(const ChannelConfig&) = delete;
    ChannelConfig& operator=(const ChannelConfig&) = delete;
    ChannelConfig(ChannelConfig&&) = delete;
    ChannelConfig& operator=(ChannelConfig&&) = delete;

    /// Message-thread call. @return false and does nothing if `channel` is
    /// outside `[0, kMaxChannels)`.
    bool setRole(int channel, ChannelRole role) noexcept {
        if (channel < 0 || channel >= kMaxChannels) {
            return false;
        }
        roles_[static_cast<std::size_t>(channel)].store(static_cast<int>(role),
                                                          std::memory_order_relaxed);
        configEpoch_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    /// Bounds-checked getter, usable from any thread. Out-of-range reads as
    /// `Unused` rather than trapping -- a caller iterating a UI list by index
    /// should never have to bounds-check separately.
    [[nodiscard]] ChannelRole role(int channel) const noexcept {
        if (channel < 0 || channel >= kMaxChannels) {
            return ChannelRole::Unused;
        }
        return static_cast<ChannelRole>(
            roles_[static_cast<std::size_t>(channel)].load(std::memory_order_relaxed));
    }

    /// Audio-callback call. Fills `out` with the current role of every
    /// channel, clipped to what this block actually received: any index at
    /// or beyond `numChannelsReceived` reads as `Unused` regardless of what
    /// was configured. See the bounds rule above.
    void snapshot(int numChannelsReceived,
                   std::array<ChannelRole, static_cast<std::size_t>(kMaxChannels)>& out) const noexcept {
        const int receivedClamped = numChannelsReceived < 0 ? 0 : numChannelsReceived;
        for (int ch = 0; ch < kMaxChannels; ++ch) {
            const auto idx = static_cast<std::size_t>(ch);
            out[idx] = (ch < receivedClamped) ? static_cast<ChannelRole>(
                                                     roles_[idx].load(std::memory_order_relaxed))
                                               : ChannelRole::Unused;
        }
    }

    /// Lowest channel index currently holding `role`, or -1 if none does.
    [[nodiscard]] int firstChannelWithRole(ChannelRole role) const noexcept {
        for (int ch = 0; ch < kMaxChannels; ++ch) {
            if (static_cast<ChannelRole>(
                    roles_[static_cast<std::size_t>(ch)].load(std::memory_order_relaxed)) == role) {
                return ch;
            }
        }
        return -1;
    }

    /// Every channel currently holding `role`, ascending by channel index,
    /// written into `out` up to its own capacity (never past it -- a caller
    /// on the message thread owns a fixed-size buffer, not a growable one).
    /// `firstChannelWithRole` stays the right call for the single-role drain
    /// (L2's one reference/one measurement path); this is the L6b routing
    /// table's own lookup, for a role that N channels may now hold at once
    /// (record §6). @return the number of channels WRITTEN into `out` --
    /// i.e. `min(matches, out.size())`, so a caller never reads past what it
    /// actually received even when more channels hold the role than `out`
    /// has room for.
    [[nodiscard]] int channelsWithRole(ChannelRole targetRole, std::span<int> out) const noexcept {
        int found = 0;
        for (int ch = 0; ch < kMaxChannels; ++ch) {
            if (static_cast<ChannelRole>(
                    roles_[static_cast<std::size_t>(ch)].load(std::memory_order_relaxed)) ==
                targetRole) {
                if (static_cast<std::size_t>(found) < out.size()) {
                    out[static_cast<std::size_t>(found)] = ch;
                }
                ++found;
            }
        }
        return std::min(found, static_cast<int>(out.size()));
    }

    /// Message-thread call, same shape as `setRole`: refuses (returns false,
    /// touches nothing, bumps no epoch) if `channel` is outside
    /// `[0, kMaxChannels)` OR `tf` is outside the same range -- there is no
    /// `kMaxTransferFunctions` at this layer (that cap belongs to B2's
    /// `AnalysisThread`, which owns the actual `Analyser` vector), so this
    /// class bounds `tf` against the one array size it does own.
    bool setTransferFunction(int channel, int tf) noexcept {
        if (channel < 0 || channel >= kMaxChannels || tf < 0 || tf >= kMaxChannels) {
            return false;
        }
        tfIndex_[static_cast<std::size_t>(channel)].store(tf, std::memory_order_relaxed);
        configEpoch_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    /// Bounds-checked getter, usable from any thread -- same contract as
    /// `role()`. Out-of-range and never-set both read as 0, the default
    /// transfer function: a channel nobody has routed yet is not an error
    /// state, it simply has not been assigned to anything past the first.
    [[nodiscard]] int transferFunction(int channel) const noexcept {
        if (channel < 0 || channel >= kMaxChannels) {
            return 0;
        }
        return tfIndex_[static_cast<std::size_t>(channel)].load(std::memory_order_relaxed);
    }

    /// Increments on every successful `setRole`. See the class comment for
    /// why this is a separate counter from `CaptureBus::epoch()`.
    [[nodiscard]] std::uint64_t configEpoch() const noexcept {
        return configEpoch_.load(std::memory_order_relaxed);
    }

private:
    std::array<std::atomic<int>, static_cast<std::size_t>(kMaxChannels)> roles_;
    /// Per-channel transfer-function index (record §6: "each transfer
    /// function names its own reference channel"). Same relaxed-atomic
    /// reasoning as `roles_` -- the class comment's "why the array is
    /// atomic, not mutex-guarded" applies verbatim here, not repeated.
    std::array<std::atomic<int>, static_cast<std::size_t>(kMaxChannels)> tfIndex_;
    std::atomic<std::uint64_t> configEpoch_{0};
};

}  // namespace rta::platform
