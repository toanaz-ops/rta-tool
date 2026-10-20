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

    // NO ring is touched here. RingBuffer::reset() writes BOTH indices, and
    // readIndex_ belongs to the consumer alone (SPSC): a reset racing a
    // consumer's discard() can leave write=0 with read=R+hop, after which
    // availableToRead() wraps to ~2^64 and the analysis thread never leaves
    // its drain loop (audit prj010-fable-audit-20261021, high). The drain of
    // the previous session's samples is the CONSUMER's job on its next tick
    // after it sees epoch() change -- see drainAsConsumer().
    // No allocation happens here either: rings_ was sized to kMaxChannels
    // once, in the constructor.

    // A device reconfiguration invalidates whatever was dropped under the
    // previous session too -- those counts described a stream that no longer
    // exists.
    for (auto& d : drops_) {
        d.store(0, std::memory_order_relaxed);
    }

    sampleRate_.store(sampleRate, std::memory_order_relaxed);
    preparedChannels_.store(clamped, std::memory_order_relaxed);
    // release: pairs with epoch()'s acquire, so a consumer that observes the
    // new epoch also observes every ring write that preceded prepare().
    epoch_.fetch_add(1, std::memory_order_release);
}

void CaptureBus::drainAsConsumer() noexcept {
    // The producer may be writing while this runs (prepare() returned and
    // setActive(true) followed). pushFromCallback writes a block channel by
    // channel, so sampling each ring's availability in turn can catch block k
    // in channel 0 but not yet in channel 1; discarding "everything available"
    // per ring would then leave channel 1 holding block k that channel 0 no
    // longer has -- a permanent one-block offset between reference and
    // measurement, which decorrelates the pair. So every ring with a writer
    // gives up the SAME count, the smallest availability among them: a lower
    // bound for each ring, so no ring is over-drained, and whatever is in
    // flight stays on all of them once the block completes.
    const int prepared = preparedChannels_.load(std::memory_order_acquire);
    std::size_t common = static_cast<std::size_t>(-1);
    for (int ch = 0; ch < prepared; ++ch) {
        if (config_.role(ch) == ChannelRole::Unused) continue;
        const auto avail = rings_[static_cast<std::size_t>(ch)]->availableToRead();
        if (avail < common) common = avail;
    }

    for (int ch = 0; ch < kMaxChannels; ++ch) {
        auto& ring = *rings_[static_cast<std::size_t>(ch)];
        const bool hasWriter = ch < prepared && config_.role(ch) != ChannelRole::Unused;
        // A ring with no writer (unused now, or measurement last session) gets
        // drained fully so it cannot serve stale samples if a later prepare()
        // brings it back.
        ring.discard(hasWriter ? common : ring.availableToRead());
    }
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
