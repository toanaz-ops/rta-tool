// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_platform_types. No JUCE, no Qt, no audio-device API.
#include "rta/platform/OutputEngine.h"

#include <algorithm>
#include <span>
#include <utility>

namespace rta::platform {

namespace {

/// Builds the 64-gate array in place via guaranteed copy elision (C++17): the
/// whole `std::array` is a single prvalue materialised directly in the
/// caller's storage, so no element is ever copied or moved -- required
/// because `gen::RampedGain` holds a `std::atomic<bool>` and is therefore
/// neither copyable nor movable (record sec.1.3). The alternative --
/// rebuilding gates by `emplace` from the device thread inside `prepare()`
/// -- is the use-after-destruction shape `CaptureBus.h` already closed on
/// the capture side (record sec.5); this constructor-only helper sidesteps
/// the question entirely by never reassigning a gate, only retargeting one
/// in place via `RampedGain::prepare` (task A).
template <std::size_t... I>
std::array<gen::RampedGain, sizeof...(I)> makeGates(double placeholderRate,
                                                      std::index_sequence<I...>) {
    return {((void)I, gen::RampedGain(placeholderRate))...};
}

}  // namespace

OutputEngine::OutputEngine()
    : slot_(std::monostate{}),
      master_(kPlaceholderSampleRate),
      outputGates_(makeGates(kPlaceholderSampleRate,
                              std::make_index_sequence<static_cast<std::size_t>(kMaxChannels)>{})),
      scratch_(kScratchCapacity, 0.0f) {}

void OutputEngine::prepare(double sampleRate, int numOutputChannels) noexcept {
    // Device thread, callback quiesced (the same slot CaptureBus::prepare
    // occupies -- see AudioIo::audioDeviceAboutToStart). Never writes slot_:
    // the message thread may be inside setSource() right now.
    outputEpoch_.fetch_add(1, std::memory_order_relaxed);
    armed_.store(false, std::memory_order_relaxed);
    sampleRate_.store(sampleRate, std::memory_order_relaxed);
    preparedOutputChannels_.store(numOutputChannels, std::memory_order_relaxed);

    master_.prepare(sampleRate);
    for (auto& gate : outputGates_) {
        gate.prepare(sampleRate);
    }
    // Every gate is now Idle (RampedGain::prepare's own contract) -- publish
    // that so a message-thread setSource() right after prepare() is not
    // refused by a stale "still playing" reading.
    publishedIdle_.store(true, std::memory_order_relaxed);
}

bool OutputEngine::sourceIsQuiescent() const noexcept {
    // Quiescent (record sec.4): disarmed, AND the callback has published
    // that the master gate reached Idle. armed_ is read back here by the
    // SAME thread that ever writes it (the message thread), so relaxed is
    // enough for that half; publishedIdle_ crosses from the audio thread, so
    // it is the acquire half of the release/acquire pair render() completes.
    return publishedIdle_.load(std::memory_order_acquire) &&
           !armed_.load(std::memory_order_relaxed);
}

bool OutputEngine::setSource(SourceVariant&& source) noexcept {
    if (!sourceIsQuiescent()) {
        return false;
    }
    slot_ = std::move(source);
    // Records the epoch this source was built for -- render() compares this,
    // relaxed, against outputEpoch() every block, and renders silence on a
    // mismatch (record sec.4's device-reconfiguration paragraph).
    slotEpoch_.store(outputEpoch_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    return true;
}

void OutputEngine::armSource() noexcept {
    // Reset BEFORE the armed_ release, per record sec.4 point 3: ordered
    // before the audio thread's first acquire-load of armed_ can observe
    // `true`, so a poller reading renderedSamples() right after arming never
    // sees a stale count from the previous arm-to-disarm window.
    renderedSamples_.store(0, std::memory_order_relaxed);
    armed_.store(true, std::memory_order_release);
}

void OutputEngine::disarmSource() noexcept {
    armed_.store(false, std::memory_order_release);
}

bool OutputEngine::routeOutput(int outputChannel, bool active) noexcept {
    if (!routing_.setRole(outputChannel, active ? OutputRole::Routed : OutputRole::None)) {
        return false;
    }
    // Same call sets the gate's target -- record sec.6: "Sets the role AND
    // the gate's target in one call." A channel just unrouted keeps
    // rendering its own falling tail (see render()'s per-channel loop, which
    // advances every gate regardless of current role) -- that fall IS the
    // click-free mute, not a special case of it.
    gen::RampedGain& gate = outputGates_[static_cast<std::size_t>(outputChannel)];
    if (active) {
        gate.requestOn();
    } else {
        gate.requestOff();
    }
    return true;
}

OutputRole OutputEngine::role(int outputChannel) const noexcept {
    return routing_.role(outputChannel);
}

std::uint64_t OutputEngine::renderedSamples() const noexcept {
    return renderedSamples_.load(std::memory_order_relaxed);
}

std::uint64_t OutputEngine::outputEpoch() const noexcept {
    return outputEpoch_.load(std::memory_order_relaxed);
}

double OutputEngine::sampleRate() const noexcept {
    return sampleRate_.load(std::memory_order_relaxed);
}

void OutputEngine::render(float* const* output, int numOutputChannels, int numSamples) noexcept {
    if (output == nullptr) {
        return;
    }
    const int channels =
        std::clamp(numOutputChannels, 0, static_cast<int>(kMaxChannels));

    // First among the output-side reads (record sec.4 point 3): the whole
    // block's behaviour hangs off this one acquire load.
    const bool armedNow = armed_.load(std::memory_order_acquire);
    const bool epochOk =
        slotEpoch_.load(std::memory_order_relaxed) == outputEpoch_.load(std::memory_order_relaxed);
    const bool wantsPlaying = armedNow && epochOk;

    if (wantsPlaying) {
        master_.requestOn();
    } else {
        master_.requestOff();
    }

    // "Armed must mean the source advances" (record sec.8, the rejected
    // "render only when routed" option) -- so the render path runs whenever
    // there is signal wanted OR the master gate has not yet finished falling
    // from a just-ended block, and does nothing (fast clear) only once both
    // are false. That fast path is the same cost class as today's clear.
    const bool needsRender = wantsPlaying || master_.state() != gen::RampedGain::State::Idle;

    if (!needsRender) {
        for (int ch = 0; ch < channels; ++ch) {
            if (output[ch] != nullptr) {
                std::fill_n(output[ch], numSamples, 0.0f);
            }
        }
        publishedIdle_.store(true, std::memory_order_release);
        return;
    }

    int offset = 0;
    int remaining = numSamples;
    while (remaining > 0) {
        const int chunk =
            remaining < static_cast<int>(kScratchCapacity) ? remaining : static_cast<int>(kScratchCapacity);
        const std::span<float> scratchSpan(scratch_.data(), static_cast<std::size_t>(chunk));

        // The SAME scratch block is what every routed output fans out from
        // (record sec.4: "one signal, correlated across routed outputs").
        std::visit(
            [&](auto& source) {
                using T = std::decay_t<decltype(source)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    std::fill_n(scratchSpan.begin(), scratchSpan.size(), 0.0f);
                } else {
                    source.process(scratchSpan);
                }
            },
            slot_);

        // Master stage first (record sec.3): what makes start/stop/swap
        // click-free, applied once, shared by every output below.
        master_.apply(scratchSpan);

        for (int ch = 0; ch < channels; ++ch) {
            gen::RampedGain& gate = outputGates_[static_cast<std::size_t>(ch)];
            float* dest = output[ch];
            if (dest == nullptr) {
                // Still advance the ramp every sample even with nowhere to
                // write it -- a channel's gate position must not depend on
                // whether the caller handed back a null pointer this block.
                for (int i = 0; i < chunk; ++i) {
                    static_cast<void>(gate.nextGain());
                }
                continue;
            }
            for (int i = 0; i < chunk; ++i) {
                dest[offset + i] = scratch_[static_cast<std::size_t>(i)] * gate.nextGain();
            }
        }

        renderedSamples_.fetch_add(static_cast<std::uint64_t>(chunk), std::memory_order_relaxed);
        offset += chunk;
        remaining -= chunk;
    }

    // Release at the end of the block, per record sec.4 point 1 -- true only
    // once the master gate has actually reached Idle this call.
    publishedIdle_.store(master_.state() == gen::RampedGain::State::Idle, std::memory_order_release);
}

}  // namespace rta::platform
