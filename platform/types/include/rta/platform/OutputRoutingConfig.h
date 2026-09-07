// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_platform_types. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/platform/ChannelConfig.h"

#include <array>
#include <atomic>
#include <cstddef>

namespace rta::platform {

/// What one OUTPUT channel is doing right now. Not `ChannelRole` (that table
/// is the INPUT side's Measurement/Reference/Unused vocabulary) -- an output
/// is simpler: either the generator drives it, or it does not.
enum class OutputRole { None, Routed };

/// `OutputEngine`'s routing table, factored out header-only (L7-OUT plan
/// OUT-R1) so `OutputEngine.cpp` stays under its own 300-line budget. Mirrors
/// `ChannelConfig`'s shape and reasoning EXACTLY: a fixed
/// `std::array<std::atomic<int>, kMaxChannels>`, relaxed store from the
/// message thread, relaxed load from the audio callback, bounds-checked
/// against `[0, kMaxChannels)` on write -- see `ChannelConfig.h`'s class
/// comment ("Why the array is atomic, not mutex-guarded") for the reasoning,
/// not repeated here. A routing flip publishes no object of its own (the
/// per-output `RampedGain`'s `target_` -- set in the same `routeOutput` call,
/// one layer up in `OutputEngine` -- is the only thing that actually has to
/// stay ordered, and it is its own independent relaxed atomic), so relaxed is
/// correct here for the identical reason it is correct in `ChannelConfig`.
class OutputRoutingConfig {
public:
    OutputRoutingConfig() noexcept {
        for (auto& slot : roles_) {
            slot.store(static_cast<int>(OutputRole::None), std::memory_order_relaxed);
        }
    }

    // Holds std::atomic members: already non-copyable and non-movable by
    // default. Spelled out here so the intent reads at the declaration.
    OutputRoutingConfig(const OutputRoutingConfig&) = delete;
    OutputRoutingConfig& operator=(const OutputRoutingConfig&) = delete;
    OutputRoutingConfig(OutputRoutingConfig&&) = delete;
    OutputRoutingConfig& operator=(OutputRoutingConfig&&) = delete;

    /// Message-thread call. @return false and does nothing if `channel` is
    /// outside `[0, kMaxChannels)`.
    bool setRole(int channel, OutputRole role) noexcept {
        if (channel < 0 || channel >= kMaxChannels) {
            return false;
        }
        roles_[static_cast<std::size_t>(channel)].store(static_cast<int>(role),
                                                          std::memory_order_relaxed);
        return true;
    }

    /// Bounds-checked getter, usable from any thread. Out-of-range reads as
    /// `None` rather than trapping -- same reasoning as `ChannelConfig::role`.
    [[nodiscard]] OutputRole role(int channel) const noexcept {
        if (channel < 0 || channel >= kMaxChannels) {
            return OutputRole::None;
        }
        return static_cast<OutputRole>(
            roles_[static_cast<std::size_t>(channel)].load(std::memory_order_relaxed));
    }

private:
    std::array<std::atomic<int>, static_cast<std::size_t>(kMaxChannels)> roles_;
};

}  // namespace rta::platform
