// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_platform_types. No JUCE, no Qt, no audio-device API.
#include "rta/platform/CaptureBus.h"

#include <span>

namespace rta::platform {

CaptureBus::CaptureBus(std::size_t ringCapacity) {
    // The ONLY allocation this class ever does (see the header's class
    // comment). All kMaxChannels rings exist from here on, whether or not
    // prepare() has ever been called for that many channels -- ring()
    // still reports an unprepared channel as absent (bounds-checked against
    // preparedChannels_, not against this vector's size).
    rings_.reserve(static_cast<std::size_t>(kMaxChannels));
    for (int i = 0; i < kMaxChannels; ++i) {
        rings_.push_back(std::make_unique<rta::dsp::RingBuffer<float>>(ringCapacity));
    }
}

void CaptureBus::prepare(double sampleRate, int numChannels) {
    const int clamped =
        numChannels < 0 ? 0 : (numChannels > kMaxChannels ? kMaxChannels : numChannels);

    // The drain: reset every ring back to empty (both indices to zero; see
    // RingBuffer::reset()) rather than rebuilding the vector. Every ring is
    // reset, not only the first `clamped` of them -- a channel that was
    // measurement last session and is unused this one must not keep serving
    // stale samples to a caller that still (mistakenly) reads it.
    // No allocation happens here: this is the whole point of sizing rings_
    // to kMaxChannels once, in the constructor, and never touching it again.
    for (auto& ring : rings_) {
        ring->reset();
    }

    // A device reconfiguration invalidates whatever was dropped under the
    // previous session too -- those counts described a stream that no longer
    // exists.
    for (auto& d : drops_) {
        d.store(0, std::memory_order_relaxed);
    }

    sampleRate_.store(sampleRate, std::memory_order_relaxed);
    preparedChannels_.store(clamped, std::memory_order_relaxed);
    epoch_.fetch_add(1, std::memory_order_relaxed);
}

void CaptureBus::pushFromCallback(const float* const* input, int numChannels, int numSamples) noexcept {
    if (!active_.load(std::memory_order_relaxed)) {
        return;
    }
    if (input == nullptr || numChannels <= 0 || numSamples <= 0) {
        return;
    }

    // Roles are bounds-checked against numChannels -- what THIS block
    // actually received -- never against what the device advertised at open
    // time (decision record).
    std::array<ChannelRole, static_cast<std::size_t>(kMaxChannels)> roles{};
    config_.snapshot(numChannels, roles);

    // Writes are additionally clipped to what this bus was prepare()d for:
    // a block reporting more channels than we have rings for must not walk
    // off the end of rings_.
    const int prepared = preparedChannels_.load(std::memory_order_relaxed);
    const int limit = numChannels < prepared ? numChannels : prepared;

    for (int ch = 0; ch < limit; ++ch) {
        const auto idx = static_cast<std::size_t>(ch);
        if (roles[idx] == ChannelRole::Unused) {
            continue;
        }
        const float* src = input[idx];
        if (src == nullptr) {
            continue;
        }
        const std::span<const float> block(src, static_cast<std::size_t>(numSamples));
        if (!rings_[idx]->write(block)) {
            // RingBuffer::write is all-or-nothing: a refused block is
            // numSamples dropped, never a partial splice. Count it, don't
            // swallow it -- a drop count nobody sees is a drop nobody fixes.
            drops_[idx].fetch_add(static_cast<std::uint64_t>(numSamples), std::memory_order_relaxed);
        }
    }
}

}  // namespace rta::platform
