// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
#include "measure/SplSession.h"

#include <algorithm>

namespace rta::measure {

SplSession::Chain::Chain(const SplConfig& config, rta::dsp::WeightingType w, double sampleRate,
                         std::size_t windowCapacity)
    : weighting(w)
    , meter(config, w, sampleRate) {
    // Reserved ONCE, here, on the message thread. feedHop never grows it.
    window.reserve(windowCapacity);
}

void SplSession::start(const SplConfig& config, double sampleRate,
                       std::span<const int> channels) {
    stop();
    config_ = config;
    sampleRate_ = sampleRate;

    // The distinct weightings the metrics named, in first-seen order. A config
    // with no metrics gets one Z chain, so a caller that only wants unweighted
    // energy is not a special case everywhere else.
    for (const SplMetricSpec& spec : config_.metrics) {
        if (std::find(weightings_.begin(), weightings_.end(), spec.weighting)
            == weightings_.end()) {
            weightings_.push_back(spec.weighting);
        }
    }
    if (weightings_.empty()) weightings_.push_back(rta::dsp::WeightingType::Z);

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
        auto s = std::make_unique<ChannelState>();
        s->chains.reserve(weightings_.size());
        for (const auto w : weightings_) {
            s->chains.emplace_back(config_, w, sampleRate, windowCapacity_);
        }
        channels_[static_cast<std::size_t>(channel)] = std::move(s);
    }
    running_ = true;
}

void SplSession::stop() noexcept {
    for (auto& state : channels_) state.reset();
    weightings_.clear();
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

const SplSession::Chain* SplSession::chain(int channel,
                                            rta::dsp::WeightingType w) const noexcept {
    const ChannelState* s = state(channel);
    if (s == nullptr) return nullptr;
    for (const Chain& c : s->chains) {
        if (c.weighting == w) return &c;
    }
    return nullptr;
}

bool SplSession::logsChannel(int channel) const noexcept {
    const ChannelState* s = state(channel);
    return running_ && s != nullptr && !s->chains.empty();
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
    const auto count = static_cast<std::uint32_t>(std::min<std::uint64_t>(delta, 0xFFFFFFFFull));
    // The COUNT rides the block, not just the bit (SPL-R1 ∧ SPL-R2, defect 5).
    // The live counter is in memory and never reaches the file, so a log that
    // carried only `Gap` would admit it lost time and be unable to say how
    // much -- and every later t_iso would be permanently early.
    //
    // EVERY chain, because the samples are gone from all of them: the loss is
    // upstream of the weighting filters, not something one chain can miss.
    for (Chain& c : s->chains) c.meter.noteDroppedSamples(count);
}

void SplSession::feedHop(int channel, std::span<const float> hop) noexcept {
    ChannelState* s = state(channel);
    if (s == nullptr) return;

    for (Chain& c : s->chains) {
        c.meter.push(hop);
        while (auto block = c.meter.poll()) {
            ++c.blocks;
            c.flagsSeen |= block->flags;
            c.droppedSamplesTotal += block->droppedSamples;

            if (c.window.size() < c.window.capacity()) {
                c.window.push_back(*block);
            } else if (!c.window.empty()) {
                // Roll: oldest out, newest in, and the span stays CONTIGUOUS
                // so combineBlocks can be handed it directly. A rotate of at
                // most kMaxWindowBlocks 40-byte blocks, once per block period,
                // is 144 KB/s at the 1 s default -- and W2-A replaces the
                // whole thing with the real ring sized from logSpanSeconds.
                std::rotate(c.window.begin(), c.window.begin() + 1, c.window.end());
                c.window.back() = *block;
            }
        }
    }
}

std::uint64_t SplSession::blockCount(int channel) const noexcept {
    const ChannelState* s = state(channel);
    if (s == nullptr || s->chains.empty()) return 0;
    // Every chain shares `blockSamples` and sees the same hops, so they
    // advance in lockstep. The MINIMUM is reported rather than the first, so a
    // chain that somehow fell behind shows up as a smaller number instead of
    // being hidden by a neighbour that did not.
    std::uint64_t blocks = s->chains.front().blocks;
    for (const Chain& c : s->chains) blocks = std::min(blocks, c.blocks);
    return blocks;
}

std::uint32_t SplSession::flagsSeen(int channel) const noexcept {
    const ChannelState* s = state(channel);
    if (s == nullptr) return 0;
    std::uint32_t flags = 0;
    for (const Chain& c : s->chains) flags |= c.flagsSeen;
    return flags;
}

std::uint64_t SplSession::droppedSamplesTotal(int channel) const noexcept {
    const ChannelState* s = state(channel);
    if (s == nullptr || s->chains.empty()) return 0;
    // NOT a sum over chains: the same loss rides every chain, so summing would
    // report it `chainCount()` times over and make a reconstructed timestamp
    // LATE instead of early -- the same defect in the other direction.
    return s->chains.front().droppedSamplesTotal;
}

std::optional<rta::meter::Block> SplSession::latestBlock(int channel) const noexcept {
    const ChannelState* s = state(channel);
    if (s == nullptr || s->chains.empty() || s->chains.front().window.empty()) {
        return std::nullopt;
    }
    return s->chains.front().window.back();
}

std::span<const rta::meter::Block> SplSession::window(int channel) const noexcept {
    const ChannelState* s = state(channel);
    if (s == nullptr || s->chains.empty()) return {};
    return s->chains.front().window;
}

std::span<const rta::meter::Block> SplSession::window(
    int channel, rta::dsp::WeightingType weighting) const noexcept {
    const Chain* c = chain(channel, weighting);
    if (c == nullptr) return {};
    return c->window;
}

std::size_t SplSession::fillMetricWindows(
    int channel, std::span<std::span<const rta::meter::Block>> out) const noexcept {
    const auto& metrics = config_.metrics;
    if (!running_ || out.size() < metrics.size()) return 0;
    for (std::size_t i = 0; i < metrics.size(); ++i) {
        // Metric i's WEIGHTING decides which window it is averaged over. A
        // weighting with no chain yields an EMPTY span, which combineBlocks
        // turns into an absent Leq -- never another weighting's numbers.
        out[i] = window(channel, metrics[i].weighting);
    }
    return metrics.size();
}

}  // namespace rta::measure
