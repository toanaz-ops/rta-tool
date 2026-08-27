// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_platform_types. No JUCE, no Qt, no audio-device API.
#include "rta/platform/CaptureBus.h"

#include <span>

namespace rta::platform {

void CaptureBus::prepare(double sampleRate, int numChannels, std::size_t ringCapacity) {
    const int clamped =
        numChannels < 0 ? 0 : (numChannels > kMaxChannels ? kMaxChannels : numChannels);

    // Rebuilding the vector -- rather than resizing in place -- is the drain:
    // a freshly constructed RingBuffer starts with both indices at zero, so
    // there is no separate "now call reset() on each one" step to forget.
    // This is the one place in this class allowed to allocate; it runs on the
    // message thread, with the callback guaranteed stopped (documented
    // precondition -- see the header).
    rings_.clear();
    rings_.reserve(static_cast<std::size_t>(clamped));
    for (int i = 0; i < clamped; ++i) {
        rings_.push_back(std::make_unique<rta::dsp::RingBuffer<float>>(ringCapacity));
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
