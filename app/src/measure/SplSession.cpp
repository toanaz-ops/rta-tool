// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
#include "measure/SplSession.h"

#include <algorithm>

namespace rta::measure {

SplSession::ChannelState::ChannelState(const SplConfig& config, double sampleRate,
                                       std::size_t windowCapacity)
    : meter(config, rta::dsp::WeightingType::A, sampleRate) {
    // Reserved ONCE, here, on the message thread. feedHop never grows it.
    window.reserve(windowCapacity);
}

void SplSession::start(const SplConfig& config, double sampleRate,
                       std::span<const int> channels) {
    stop();
    config_ = config;
    sampleRate_ = sampleRate;

    // The window holds the longest metric's own span, so every metric can be
    // recomputed from it, bounded by kMaxWindowBlocks so a misconfigured
    // metric cannot ask for an eight-hour vector in Wave 0.
    std::uint64_t longest = 1;
    for (const SplMetricSpec& spec : config_.metrics) {
        longest = std::max(longest, spec.windowBlocks);
    }
    windowCapacity_ = std::min(static_cast<std::size_t>(longest), kMaxWindowBlocks);

    for (const int channel : channels) {
        if (channel < 0 || static_cast<std::size_t>(channel) >= kMaxLoggedChannels) continue;
        channels_[static_cast<std::size_t>(channel)] =
            std::make_unique<ChannelState>(config_, sampleRate, windowCapacity_);
    }
    running_ = true;
}

void SplSession::stop() noexcept {
    for (auto& state : channels_) state.reset();
    running_ = false;
}

SplSession::ChannelState* SplSession::state(int channel) noexcept {
    if (channel < 0 || static_cast<std::size_t>(channel) >= kMaxLoggedChannels) return nullptr;
    return channels_[static_cast<std::size_t>(channel)].get();
}

const SplSession::ChannelState* SplSession::state(int channel) const noexcept {
    if (channel < 0 || static_cast<std::size_t>(channel) >= kMaxLoggedChannels) return nullptr;
    return channels_[static_cast<std::size_t>(channel)].get();
}

bool SplSession::logsChannel(int channel) const noexcept {
    return running_ && state(channel) != nullptr;
}

void SplSession::noteDropCount(int channel, std::uint64_t busDropCount) noexcept {
    ChannelState* s = state(channel);
    if (s == nullptr) return;

    if (!s->dropBaselineSet) {
        // A bus that had already dropped samples before logging began has not
        // lost anything from THIS log. Baseline only.
        s->lastBusDropCount = busDropCount;
        s->dropBaselineSet = true;
        return;
    }
    if (busDropCount <= s->lastBusDropCount) return;

    const std::uint64_t delta = busDropCount - s->lastBusDropCount;
    s->lastBusDropCount = busDropCount;
    // The COUNT rides the block, not just the bit (SPL-R1 ∧ SPL-R2, defect 5).
    // The live counter is in memory and never reaches the file, so a log that
    // carried only `Gap` would admit it lost time and be unable to say how
    // much -- and every later t_iso would be permanently early.
    s->meter.noteDroppedSamples(
        static_cast<std::uint32_t>(std::min<std::uint64_t>(delta, 0xFFFFFFFFull)));
}

void SplSession::feedHop(int channel, std::span<const float> hop) noexcept {
    ChannelState* s = state(channel);
    if (s == nullptr) return;

    s->meter.push(hop);
    while (auto block = s->meter.poll()) {
        ++s->blocks;
        s->flagsSeen |= block->flags;
        s->droppedSamplesTotal += block->droppedSamples;

        if (s->window.size() < s->window.capacity()) {
            s->window.push_back(*block);
        } else if (!s->window.empty()) {
            // Roll: oldest out, newest in, and the span stays CONTIGUOUS so
            // combineBlocks can be handed it directly. A rotate of at most
            // kMaxWindowBlocks 40-byte blocks, once per block period, is
            // 144 KB/s at the 1 s default -- and W2-A replaces the whole
            // thing with the real ring sized from logSpanSeconds.
            std::rotate(s->window.begin(), s->window.begin() + 1, s->window.end());
            s->window.back() = *block;
        }
    }
}

std::uint64_t SplSession::blockCount(int channel) const noexcept {
    const ChannelState* s = state(channel);
    return s == nullptr ? 0 : s->blocks;
}

std::uint32_t SplSession::flagsSeen(int channel) const noexcept {
    const ChannelState* s = state(channel);
    return s == nullptr ? 0 : s->flagsSeen;
}

std::uint64_t SplSession::droppedSamplesTotal(int channel) const noexcept {
    const ChannelState* s = state(channel);
    return s == nullptr ? 0 : s->droppedSamplesTotal;
}

std::optional<rta::meter::Block> SplSession::latestBlock(int channel) const noexcept {
    const ChannelState* s = state(channel);
    if (s == nullptr || s->window.empty()) return std::nullopt;
    return s->window.back();
}

std::span<const rta::meter::Block> SplSession::window(int channel) const noexcept {
    const ChannelState* s = state(channel);
    if (s == nullptr) return {};
    return s->window;
}

}  // namespace rta::measure
