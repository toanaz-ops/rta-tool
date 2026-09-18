// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. JUCE (through AnalysisThread.h only).
//
// AnalysisThread.cpp's SPL half, split off at the 400-line hard cap (lane L6a
// task W0-D). The same class, a second translation unit -- the shape
// AlignmentWizard.cpp / AlignmentWizardSignals.cpp already ships, and the
// seam the plan itself names: drain versus feed. Everything here is the
// THREAD HANDOVER and the PUBLISHED COUNTERS; the measurement itself is
// rta::measure::SplSession, which is JUCE-free and proven with
// RTA_BUILD_APP=OFF.
#include "measure/AnalysisThread.h"

#include <algorithm>

namespace rta::measure {

void AnalysisThread::applyPendingSplRequest() {
    if (!splRequestPending_.exchange(false, std::memory_order_acquire)) return;

    // The lock is taken only on the tick a request actually arrived, never on
    // every drain -- and never from the audio callback, which cannot reach
    // this function at all.
    SplConfig config;
    std::vector<int> channels;
    bool enable = false;
    {
        const std::lock_guard<std::mutex> lock(splRequestLock_);
        config = splRequestConfig_;
        channels = splRequestChannels_;
        enable = splRequestEnable_;
    }
    if (enable) {
        splSession_.start(config, bus_.sampleRate(), channels);
    } else {
        splSession_.stop();
    }
    for (auto& count : splBlockCounts_) count.store(0, std::memory_order_relaxed);
    for (auto& flags : splFlagsSeen_) flags.store(0, std::memory_order_relaxed);
    for (auto& dropped : splDroppedSamples_) dropped.store(0, std::memory_order_relaxed);
}

void AnalysisThread::feedSpl(int channel) {
    if (!splSession_.logsChannel(channel)) return;

    // The bus's own cumulative drop count, read BEFORE the hop is fed, so a
    // loss that happened before these samples rides the block they land in.
    // `CaptureBus::dropCount` is a relaxed atomic the callback increments
    // (platform/types/src/CaptureBus.cpp) -- reading it here is the whole of
    // this thread's knowledge that time went missing.
    splSession_.noteDropCount(channel, bus_.dropCount(channel));
    splSession_.feedHop(channel, channelScratch_[static_cast<std::size_t>(channel)]);

    const auto slot = static_cast<std::size_t>(channel);
    if (slot < splBlockCounts_.size()) {
        splBlockCounts_[slot].store(splSession_.blockCount(channel), std::memory_order_relaxed);
        splFlagsSeen_[slot].store(splSession_.flagsSeen(channel), std::memory_order_relaxed);
        splDroppedSamples_[slot].store(splSession_.droppedSamplesTotal(channel),
                                       std::memory_order_relaxed);
    }
}

void AnalysisThread::enableSplLogging(const SplConfig& config, std::span<const int> channels) {
    {
        const std::lock_guard<std::mutex> lock(splRequestLock_);
        splRequestConfig_ = config;
        splRequestChannels_.assign(channels.begin(), channels.end());
        splRequestEnable_ = true;
    }
    splRequestPending_.store(true, std::memory_order_release);
}

void AnalysisThread::disableSplLogging() {
    {
        const std::lock_guard<std::mutex> lock(splRequestLock_);
        splRequestEnable_ = false;
        splRequestChannels_.clear();
    }
    splRequestPending_.store(true, std::memory_order_release);
}

std::uint64_t AnalysisThread::splBlockCount(int channel) const noexcept {
    if (channel < 0 || static_cast<std::size_t>(channel) >= splBlockCounts_.size()) return 0;
    return splBlockCounts_[static_cast<std::size_t>(channel)].load(std::memory_order_relaxed);
}

std::uint32_t AnalysisThread::splFlagsSeen(int channel) const noexcept {
    if (channel < 0 || static_cast<std::size_t>(channel) >= splFlagsSeen_.size()) return 0;
    return splFlagsSeen_[static_cast<std::size_t>(channel)].load(std::memory_order_relaxed);
}

void AnalysisThread::fillSplPublishInput(
    SplPublishInput& input, std::span<std::span<const rta::meter::Block>> metricWindows) const {
    // `config == nullptr` is the ONE way to say nothing is logging (W0-C C1),
    // so it is left alone on every path that does not find a completed block.
    const SplConfig* config = splSession_.config();
    if (config == nullptr) return;

    // Wave 0 publishes the FIRST logged channel. One SPL block per Snapshot is
    // what `Snapshot::spl` is (record §9's payload is per-channel and the pane
    // is W2-D); a multi-channel publish is Wave 2's, and picking the lowest
    // logged channel is a rule a reader can state rather than whichever one
    // the container happened to order first.
    int channel = -1;
    for (int ch = 0; ch < static_cast<int>(SplSession::kMaxLoggedChannels); ++ch) {
        if (splSession_.logsChannel(ch)) {
            channel = ch;
            break;
        }
    }
    if (channel < 0) return;

    auto latest = splSession_.latestBlock(channel);
    if (!latest.has_value()) return;  // a session with no closed block yet

    input.config = config;
    input.sampleRate = splSession_.sampleRate();
    input.latestBlock = latest;
    input.window = splSession_.window(channel);
    input.refusedMetrics = splSession_.refusedMetrics();
    // PER-METRIC windows: metric i's weighting decides which chain it is
    // averaged over, and without this an A-weighted and a C-weighted metric
    // would read the same numbers.
    const std::size_t filled = splSession_.fillMetricWindows(channel, metricWindows);
    input.metricWindows = metricWindows.first(filled);
}

std::uint64_t AnalysisThread::splDroppedSamples(int channel) const noexcept {
    if (channel < 0 || static_cast<std::size_t>(channel) >= splDroppedSamples_.size()) return 0;
    return splDroppedSamples_[static_cast<std::size_t>(channel)].load(std::memory_order_relaxed);
}

}  // namespace rta::measure
