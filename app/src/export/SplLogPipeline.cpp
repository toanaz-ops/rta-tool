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
    // A fresh session starts with a fresh drop count, not the previous
    // session's tally left behind by writerLoop()'s own shutdown snapshot.
    for (auto& dropped : lastDropped_) dropped.store(0, std::memory_order_relaxed);
    // Same for the previous session's open-failure flag (finding 6): a fresh
    // log's file has not been attempted yet, so it starts clean regardless
    // of whether the PREVIOUS session ever failed to open one.
    for (auto& failed : lastWriteFailed_) failed.store(false, std::memory_order_relaxed);

    // Allocate the ring only -- no SplLogWriter, no open file, here. That is
    // ALL setupWriters() needs from `pendingParams_` to build the exact same
    // sinks_ entries the old inline loop did, just on the writer thread
    // instead (finding 4). See this class's own header comment on `enable`.
    for (const auto& spec : params.channels) {
        if (spec.channel < 0 || static_cast<std::size_t>(spec.channel) >= kMaxLoggedChannels) {
            continue;  // out of range: silently ignored, SplSession::start's own convention
        }
        auto sink = std::make_unique<ChannelSink>();
        sink->channel = spec.channel;
        sink->ring = std::make_unique<Ring>(params.queueCapacityBlocks);
        sinks_[static_cast<std::size_t>(spec.channel)] = std::move(sink);
    }

    // Written BEFORE the writer thread starts: std::thread's constructor
    // synchronizes-with the start of the new thread's execution, so
    // setupWriters() (running on that thread) is guaranteed to see this
    // exact value with no lock needed (SplLogPipeline.h's own comment on
    // `pendingParams_`).
    pendingParams_ = params;

    running_.store(true, std::memory_order_release);
    writerThread_ = std::thread([this] { writerLoop(); });
}

void SplLogPipeline::disable() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;  // already disabled -- enable() always calls this first, so nothing to join
    }
    // writerLoop()'s own shutdown sequence does the final drain, snapshots
    // lastDropped_ and resets every sink (closing its file) BEFORE this join
    // returns -- all on the writer thread, none of it here (finding 4's
    // "drain/close happen on writer thread" half).
    if (writerThread_.joinable()) writerThread_.join();
}

void SplLogPipeline::setupWriters() {
    std::vector<std::string> channelFiles;
    channelFiles.reserve(pendingParams_.channels.size());

    for (const auto& spec : pendingParams_.channels) {
        if (spec.channel < 0 || static_cast<std::size_t>(spec.channel) >= kMaxLoggedChannels) {
            continue;
        }
        auto& sinkPtr = sinks_[static_cast<std::size_t>(spec.channel)];
        if (!sinkPtr) {
            continue;  // enable() skipped this spec for the same out-of-range reason
        }
        sinkPtr->writer = writerFactory_(spec.basePath, pendingParams_.config, spec.info,
                                         pendingParams_.config.segmentBlocks);
        // Finding 6: the constructor's own openSegment() call already ran
        // (SplLog.h's own comment: "the constructor's own first call"), so
        // this is the earliest point a failed FIRST segment is visible.
        if (sinkPtr->writer->writeFailed()) {
            sinkPtr->writeFailed.store(true, std::memory_order_relaxed);
        }
        channelFiles.push_back(sinkPtr->writer->segmentPaths().back());
    }

    if (!pendingParams_.sessionHeaderPath.empty()) {
        // The session-wide facts are identical across every channel (record
        // §10: one session, one clock, one block size) -- read off the
        // first spec rather than repeated per channel.
        SplSessionHeaderInfo info;
        info.startedAtUnixMs = pendingParams_.startedAtUnixMs;
        if (!pendingParams_.channels.empty()) {
            info.sampleRate = pendingParams_.channels.front().info.sampleRate;
            info.blockSamples = pendingParams_.channels.front().info.blockSamples;
        }
        info.channelFiles = std::move(channelFiles);
        writeSessionHeaderFile(pendingParams_.sessionHeaderPath, info);
    }
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
        // Finding 6/round-3 finding 2: a LATER segment can fail to open on
        // rotation, or a `write()` call itself can fail mid-session (disk
        // full, handle closed underneath this writer) -- checked once per
        // drain pass rather than once per `write()` call, cheap either way
        // since this is a plain bool read.
        if (sinkPtr->writer->writeFailed()) {
            sinkPtr->writeFailed.store(true, std::memory_order_relaxed);
        }
    }
    return any;
}

void SplLogPipeline::writerLoop() {
    // Finding 4: every file this session opens, opens HERE -- before the
    // drain loop starts, on this thread, never on whatever thread called
    // enable().
    setupWriters();

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

    // Finding 4's other half: snapshot the drop count and close every file
    // HERE too, on the writer thread -- `disable()` on the calling thread
    // only ever joins, it no longer touches `sinks_` itself.
    for (std::size_t i = 0; i < sinks_.size(); ++i) {
        if (sinks_[i]) {
            lastDropped_[i].store(sinks_[i]->dropped.load(std::memory_order_relaxed),
                                   std::memory_order_relaxed);
            lastWriteFailed_[i].store(sinks_[i]->writeFailed.load(std::memory_order_relaxed),
                                      std::memory_order_relaxed);
        }
        sinks_[i].reset();
    }
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
    const auto slot = static_cast<std::size_t>(channel);
    const auto& sinkPtr = sinks_[slot];
    if (!sinkPtr) return lastDropped_[slot].load(std::memory_order_relaxed);
    return sinkPtr->dropped.load(std::memory_order_relaxed);
}

bool SplLogPipeline::writeFailed(int channel) const noexcept {
    if (channel < 0 || static_cast<std::size_t>(channel) >= kMaxLoggedChannels) return false;
    const auto slot = static_cast<std::size_t>(channel);
    const auto& sinkPtr = sinks_[slot];
    if (!sinkPtr) return lastWriteFailed_[slot].load(std::memory_order_relaxed);
    return sinkPtr->writeFailed.load(std::memory_order_relaxed);
}

}  // namespace rta::splexport
