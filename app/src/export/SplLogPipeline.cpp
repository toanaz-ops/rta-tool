// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
// Lane L6a task W2-E2a. See SplLogPipeline.h for the thread-boundary
// rationale; this file is only the mechanics of that boundary.
#include "export/SplLogPipeline.h"

#include <chrono>
#include <utility>

namespace rta::splexport {

namespace {
// Polled rather than woken by a condition variable: the producer must never
// touch a lock (this file's own header comment), and 5 ms against a
// `blockSeconds` default of 1 s is well under one block period -- the writer
// thread is never the reason a log looks "live but stale".
constexpr std::chrono::milliseconds kIdleSleep{ 5 };
}  // namespace

void SplLogPipeline::enable(const SplLogEnableParams& params) {
    disable();  // stop and join any previous session first -- never appended to

    std::vector<std::string> channelFiles;
    channelFiles.reserve(params.channels.size());

    for (const auto& spec : params.channels) {
        if (spec.channel < 0 || static_cast<std::size_t>(spec.channel) >= kMaxLoggedChannels) {
            continue;  // out of range: silently ignored, SplSession::start's own convention
        }
        auto sink = std::make_unique<ChannelSink>();
        sink->channel = spec.channel;
        sink->ring = std::make_unique<Ring>(params.queueCapacityBlocks);
        sink->writer = std::make_unique<SplLogWriter>(spec.basePath, params.config, spec.info,
                                                       params.config.segmentBlocks);
        channelFiles.push_back(sink->writer->segmentPaths().back());
        sinks_[static_cast<std::size_t>(spec.channel)] = std::move(sink);
    }

    if (!params.sessionHeaderPath.empty()) {
        // The session-wide facts are identical across every channel (record
        // §10: one session, one clock, one block size) -- read off the
        // first spec rather than repeated per channel.
        SplSessionHeaderInfo info;
        info.startedAtUnixMs = params.startedAtUnixMs;
        if (!params.channels.empty()) {
            info.sampleRate = params.channels.front().info.sampleRate;
            info.blockSamples = params.channels.front().info.blockSamples;
        }
        info.channelFiles = std::move(channelFiles);
        writeSessionHeaderFile(params.sessionHeaderPath, info);
    }

    running_.store(true, std::memory_order_release);
    writerThread_ = std::thread([this] { writerLoop(); });
}

void SplLogPipeline::disable() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;  // already disabled -- enable() always calls this first, so nothing to join
    }
    if (writerThread_.joinable()) writerThread_.join();
    for (auto& sink : sinks_) sink.reset();
}

bool SplLogPipeline::drainOnce() {
    bool any = false;
    rta::meter::Block scratch;
    for (auto& sinkPtr : sinks_) {
        if (!sinkPtr) continue;
        // Drain everything currently queued in one pass, not one block per
        // tick -- a writer thread that fell behind during a burst catches up
        // in one iteration instead of one block per kIdleSleep.
        while (sinkPtr->ring->read(std::span<rta::meter::Block>(&scratch, 1)) != 0) {
            sinkPtr->writer->write(scratch);
            any = true;
        }
    }
    return any;
}

void SplLogPipeline::writerLoop() {
    while (running_.load(std::memory_order_acquire)) {
        if (!drainOnce()) {
            std::this_thread::sleep_for(kIdleSleep);
        }
    }
    // Shutdown drains then joins (W2-E2a's own acceptance): one more pass
    // after `running_` goes false catches anything the producer pushed
    // between this loop's last check above and `disable()` flipping the
    // flag -- `disable()` does not join until this function returns.
    drainOnce();
}

void SplLogPipeline::pushBlock(int channel, const rta::meter::Block& block) noexcept {
    if (channel < 0 || static_cast<std::size_t>(channel) >= kMaxLoggedChannels) return;
    auto& sinkPtr = sinks_[static_cast<std::size_t>(channel)];
    if (!sinkPtr) return;
    if (!sinkPtr->ring->write(std::span<const rta::meter::Block>(&block, 1))) {
        sinkPtr->dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

std::uint64_t SplLogPipeline::droppedBlocks(int channel) const noexcept {
    if (channel < 0 || static_cast<std::size_t>(channel) >= kMaxLoggedChannels) return 0;
    const auto& sinkPtr = sinks_[static_cast<std::size_t>(channel)];
    if (!sinkPtr) return 0;
    return sinkPtr->dropped.load(std::memory_order_relaxed);
}

}  // namespace rta::splexport
