// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. JUCE-free itself (SplLogPipeline.h,
// SplChannelState.h and SplSession.h all are); AnalysisThread.h is the ONE
// JUCE-owning caller.
//
// LOW follow-up batch, item 16. Lane L6a's whole SPL feed state (tasks W0-D,
// W2-E1, W2-E2a), factored out of `AnalysisThread` itself so that class's own
// header stays clear of the 400-line hard cap -- these members carried no
// JUCE dependency of their own even inside AnalysisThread.h, so lifting them
// into their own JUCE-free header changes nothing about what the class can
// do, only where the field list lives.
#pragma once

#include "export/SplLogPipeline.h"
#include "measure/SplChannelState.h"
#include "measure/SplConfig.h"
#include "measure/SplSession.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace rta::measure {

/// ANALYSIS-THREAD-ONLY except where a field's own comment says otherwise
/// (the request-handover fields below, guarded by `requestLock` and set by
/// the message thread; the published atomics, written by the analysis thread
/// and read from any thread). `AnalysisThreadSpl.cpp` is the one translation
/// unit that reads and writes every field here -- see that file and
/// `AnalysisThread.h`'s own class comment for the full threading argument.
struct AnalysisThreadSplState {
    // --- task W0-D: the session itself ------------------------------------
    // JUCE-free (measure/SplSession.h), so the block clock, the gap
    // arithmetic and the window are all provable with RTA_BUILD_APP=OFF.
    SplSession session;  // analysis-thread-only

    /// Station-4 fix round (PR #31, round 3, verifier finding 1, MEDIUM): the
    /// epoch the CURRENT SPL session (state and/or log) was started at,
    /// recorded by `applyPendingSplRequest()` every time it runs.
    /// `AnalysisThread::feedSpl()` compares this against its own `lastEpoch_`
    /// and freezes the instant they differ -- see AnalysisThreadSpl.cpp's own
    /// comments on both functions for why a poll-driven composition root (up
    /// to 500 ms, MainComponentSpl.cpp) cannot be trusted to react to a
    /// device reconfiguration before more audio arrives at the new rate.
    std::uint64_t sessionEpoch = 0;

    mutable std::mutex requestLock;
    SplConfig requestConfig;
    std::vector<int> requestChannels;
    bool requestEnable = false;
    /// W2-E2a: empty means "state only, nothing touches disk" -- see
    /// `AnalysisThread::enableSplLogging`'s own comment. Guarded by
    /// `requestLock`, same as the three members above it.
    std::string requestLogDirectory;
    std::optional<double> requestCalibratorLevelDb;  // enableSplLogging's own, guarded likewise
    /// The handover. Same shape as `AnalysisThread::locateArmRequested_`: the
    /// message thread writes under the lock and releases this flag; the
    /// analysis thread acquires it once per drain and takes the lock only
    /// then, so no drain that has nothing to pick up pays for one.
    std::atomic<bool> requestPending{false};

    /// Published per channel, for the same reason `AnalysisThread::
    /// routeHopCounts_` is: a plain counter written by this thread and read
    /// from another would be a data race even where a stale value would look
    /// harmless.
    std::array<std::atomic<std::uint64_t>, SplSession::kMaxLoggedChannels> blockCounts{};
    std::array<std::atomic<std::uint32_t>, SplSession::kMaxLoggedChannels> flagsSeen{};
    std::array<std::atomic<std::uint64_t>, SplSession::kMaxLoggedChannels> droppedSamples{};

    // --- task W2-E1: the per-channel SPL state ----------------------------
    // One JUCE-free SplChannelState per logged channel (history, alarms,
    // dose, Ln), allocated in applyPendingSplRequest() -- the same moment
    // session.start() allocates its own chains -- and never resized
    // afterward. ANALYSIS-THREAD-ONLY, same as `session` above: fed from
    // feedSpl(), read from fillSplPublishInput().
    std::array<std::unique_ptr<SplChannelState>, SplSession::kMaxLoggedChannels> channelStates;

    // --- task W2-E2a: the log-writing pipeline ----------------------------
    // JUCE-free (app/src/export/SplLogPipeline.h), so the queue/writer
    // mechanics are provable with RTA_BUILD_APP=OFF. Started/stopped from
    // applyPendingSplRequest(), the same moment `session` itself is;
    // pushBlock() is called from feedSpl(), on this thread alone -- see that
    // class's own THREADING comment for why no lock guards it here.
    rta::splexport::SplLogPipeline logPipeline;

    /// Published per channel, same shape as `droppedSamples` above: a plain
    /// mirror of `logPipeline.droppedBlocks(channel)`, refreshed in feedSpl()
    /// so the message thread never reads `logPipeline` itself (that object is
    /// analysis-thread-only).
    std::array<std::atomic<std::uint64_t>, SplSession::kMaxLoggedChannels> logDroppedBlocks{};
    /// Station-4 fix round (PR #31, finding 6): same mirror shape as
    /// `logDroppedBlocks`, over `logPipeline.writeFailed(channel)`.
    std::array<std::atomic<bool>, SplSession::kMaxLoggedChannels> logWriteFailed{};

    /// logWriteFailed's mirror shape, opposite direction (message thread
    /// writes, analysis thread reads): task W2-E2b part A's calibration
    /// verdict.
    std::array<std::atomic<bool>, SplSession::kMaxLoggedChannels> calibrationInvalid{};
};

}  // namespace rta::measure
