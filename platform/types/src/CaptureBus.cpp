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

    // NO ring is touched here: RingBuffer::reset() writes readIndex_, which
    // only the consumer may write (audit prj010-fable-audit-20261021, high).
    // The previous session's samples are dropped by the consumer's resync,
    // behind the producer gate raised below. No allocation either: rings_ was
    // sized to kMaxChannels once, in the constructor.

    // A device reconfiguration invalidates whatever was dropped under the
    // previous session too -- those counts described a stream that no longer
    // exists.
    for (auto& d : drops_) {
        d.store(0, std::memory_order_relaxed);
    }
    resyncFrames_.store(0, std::memory_order_relaxed);

    sampleRate_.store(sampleRate, std::memory_order_relaxed);
    preparedChannels_.store(clamped, std::memory_order_relaxed);
    // Gate the producer BEFORE the epoch moves: a consumer that sees the new
    // epoch must find the gate up. Only with a consumer to lower it.
    if (consumerAttached_.load(std::memory_order_acquire)) {
        raiseGate();
    }
    // release: pairs with epoch()'s acquire, so a consumer that observes the
    // new epoch also observes every ring write that preceded prepare().
    epoch_.fetch_add(1, std::memory_order_release);
}

void CaptureBus::raiseGate() noexcept {
    // The mark goes in BEFORE the gate rises: only a callback that observes
    // the gate can move gateSeen_ past it.
    gateSeenAtRaise_.store(gateSeen_.load(std::memory_order_acquire), std::memory_order_relaxed);
    resyncSeq_.fetch_add(1, std::memory_order_release);
    resyncPending_.store(true, std::memory_order_release);
}

std::uint64_t CaptureBus::writerMask() const noexcept {
    static_assert(kMaxChannels <= 64, "writerMask packs one bit per channel into 64 bits");
    std::uint64_t mask = 0;
    for (int ch = 0; ch < kMaxChannels; ++ch) {
        if (config_.role(ch) != ChannelRole::Unused) mask |= std::uint64_t{1} << ch;
    }
    return mask;
}

void CaptureBus::attachConsumer() noexcept {
    consumerWriterMask_ = writerMask();
    consumerAttached_.store(true, std::memory_order_release);
}

void CaptureBus::detachConsumer() noexcept {
    consumerAttached_.store(false, std::memory_order_release);
    resyncPending_.store(false, std::memory_order_release);
}

bool CaptureBus::serviceResyncAsConsumer() noexcept {
    // A role assigned or cleared while the device runs changes WHICH rings
    // are written: the new one starts empty while the others hold a backlog,
    // so the pair is offset for good. Raise the gate.
    const std::uint64_t mask = writerMask();
    if (mask != consumerWriterMask_) {
        consumerWriterMask_ = mask;
        raiseGate();
    }
    if (!resyncPending_.load(std::memory_order_acquire)) {
        consumerWaiting_ = false;
        return false;
    }

    // A callback already past its gate check when the flag rose may still be
    // mid-block, and a drain that raced it would split that block across the
    // rings. Callbacks run one at a time, so one that SEES the gate up proves
    // the earlier one returned: wait for gateSeen_ to move. No callbacks at
    // all (bus inactive) means nothing can be in flight; and the wait is
    // bounded, so a stalled device cannot keep the consumer from draining.
    const auto now = std::chrono::steady_clock::now();
    if (!consumerWaiting_) {
        consumerWaiting_ = true;
        consumerRaisedAt_ = now;
    }
    const bool acknowledged = gateSeen_.load(std::memory_order_acquire) !=
                              gateSeenAtRaise_.load(std::memory_order_relaxed);
    const bool idle = !active_.load(std::memory_order_relaxed);
    const bool timedOut = now - consumerRaisedAt_ >= consumerAckTimeout_;
    if (!acknowledged && !idle && !timedOut) {
        return false;
    }
    if (timedOut && !acknowledged && !idle) {
        resyncAckTimeouts_.fetch_add(1, std::memory_order_relaxed);
    }

    // What the drain throws away is lost time, like the frames the gate
    // refused; the SPL feed reports both (resyncLostFrames()).
    resyncLostFrames_.fetch_add(drainAsConsumer(), std::memory_order_relaxed);
    resyncSeq_.fetch_add(1, std::memory_order_release);
    consumerWaiting_ = false;
    // Lowered only AFTER the drain: lowering first would let the callback
    // write into rings still being emptied.
    resyncPending_.store(false, std::memory_order_release);
    return true;
}

std::size_t CaptureBus::drainAsConsumer() noexcept {
    // Every ring, including those with no writer now (measurement last
    // session): none may serve stale samples if a later role brings it back.
    std::size_t largest = 0;
    for (auto& ring : rings_) {
        const std::size_t discarded = ring->discard(ring->availableToRead());
        if (discarded > largest) largest = discarded;
    }
    return largest;
}

void CaptureBus::pushFromCallback(const float* const* input, int numChannels, int numSamples) noexcept {
    if (!active_.load(std::memory_order_relaxed)) {
        return;
    }
    if (input == nullptr || numChannels <= 0 || numSamples <= 0) {
        return;
    }

    // Resync gate: while the consumer is emptying the rings this block must
    // not land in some of them and not others. One acquire load, no lock.
    if (resyncPending_.load(std::memory_order_acquire)) {
        // Seeing the gate up is the acknowledgement the consumer waits for.
        gateSeen_.fetch_add(1, std::memory_order_relaxed);
        resyncFrames_.fetch_add(static_cast<std::uint64_t>(numSamples), std::memory_order_relaxed);
        resyncLostFrames_.fetch_add(static_cast<std::uint64_t>(numSamples), std::memory_order_relaxed);
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
