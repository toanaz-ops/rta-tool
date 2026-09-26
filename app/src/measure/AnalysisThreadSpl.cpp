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

// THE WINDOW ARRAY'S BOUND IS A GATE, NOT A CONVENTION (round-2 verifier).
//
// `AnalysisThread::publishIfDue` declares its per-metric window storage as
// `std::array<..., kMaxSplMetricWindows>`, and that header constant is written
// as `= SplConfig::kMaxMetrics`. Written that way it is only a CONVENTION: a
// literal 16 there with `kMaxMetrics` raised to 24 compiles, every test stays
// green, and the eight metrics past the array's end silently lose their
// windows -- which `buildSplBlockView`'s no-fallback rule then publishes as
// ABSENT readings. This assertion is in the same translation unit as the SPL
// feed's own array declarations, so the drift fails the BUILD rather than a
// test somebody has to think to write.
//
// The behavioural half is `app/tests_juce/test_spl_drain.cpp`'s D5, which
// sizes its own buffer from THIS constant and asserts every configured metric
// gets a window -- measured red under the same mutation, reading
// "kMaxSplMetricWindows = 16, kMaxMetrics = 24, metrics = 24, filled = 16".
//
// NOTE, because it is easy to assume otherwise: the OFF-build case
// `app/tests/test_spl_publish.cpp` "every metric the config can express gets a
// PRESENT reading" does NOT catch this drift. `AnalysisThread.h` includes
// JUCE, so that file cannot name this constant, and it sizes its buffer from
// `SplConfig::kMaxMetrics` instead -- measured, it stays GREEN under the
// literal-16 mutation. So this assertion and D5 are the only two guards on the
// relationship, and the one CI runs is this one, at compile time.
static_assert(AnalysisThread::kMaxSplMetricWindows == SplConfig::kMaxMetrics,
              "the per-metric window array must be sized by SplConfig::kMaxMetrics -- a "
              "smaller array silently drops the metrics past its end to ABSENT readings");

void AnalysisThread::applyPendingSplRequest() {
    if (!splState_.requestPending.exchange(false, std::memory_order_acquire)) return;

    // Station-4 fix round (PR #31, round 3, finding 1, MEDIUM: the epoch
    // race). `rebuildAnalysersIfEpochChanged()` always runs before `drain()`
    // (and so before this function) in the same tick -- AnalysisThread.cpp's
    // own runBody() -- so `lastEpoch_` is never stale relative to here.
    // Capturing it NOW is what lets feedSpl() tell "this session's own
    // epoch" apart from "the bus reconfigured out from under it since".
    splState_.sessionEpoch = lastEpoch_;

    // The lock is taken only on the tick a request actually arrived, never on
    // every drain -- and never from the audio callback, which cannot reach
    // this function at all.
    SplConfig config;
    std::vector<int> channels;
    bool enable = false;
    std::string logDirectory;
    std::optional<double> calibratorLevelDb;
    {
        const std::lock_guard<std::mutex> lock(splState_.requestLock);
        config = splState_.requestConfig;
        channels = splState_.requestChannels;
        enable = splState_.requestEnable;
        logDirectory = splState_.requestLogDirectory;
        calibratorLevelDb = splState_.requestCalibratorLevelDb;
    }
    if (enable) {
        splState_.session.start(config, bus_.sampleRate(), channels);
    } else {
        splState_.session.stop();
    }

    // W2-E1: one SplChannelState per logged channel, allocated HERE --
    // exactly where splState_.session.start() allocates its own chains and
    // windows -- and never resized afterward. A disable (or a fresh enable
    // replacing a previous session) drops every existing state, so a
    // channel's history/alarms/dose/Ln never survive across two sessions
    // that happen to share a channel number.
    for (auto& state : splState_.channelStates) state.reset();
    if (enable) {
        for (const int ch : channels) {
            if (ch < 0 || static_cast<std::size_t>(ch) >= splState_.channelStates.size()) continue;
            splState_.channelStates[static_cast<std::size_t>(ch)] =
                std::make_unique<SplChannelState>(config, bus_.sampleRate());
        }
    }

    // W2-E2a: the log-writing pipeline, started/stopped in lockstep with
    // splState_.session itself -- a fresh log every time this runs with
    // enable == true, never appended to (record §10: "a weighting change
    // starts a new log"), and an empty logDirectory means state only (see
    // enableSplLogging's own comment).
    //
    // BEFORE the counter resets below, deliberately (fix round, station-4
    // self-check): `splState_.logPipeline.disable()` BLOCKS until its writer
    // thread has drained and joined, so a caller polling `splBlockCount()`
    // back to 0 from another thread (the same "wait for the request to
    // land" idiom `AnalysisThread`'s own tests already use) must not be able
    // to observe that 0 before the pipeline has actually finished closing
    // its files -- ordering this after the reset would let a polling reader
    // race a writer thread that is still mid-drain.
    if (enable && !logDirectory.empty()) {
        rta::splexport::SplLogEnableParams params;
        params.config = config;
        params.startedAtUnixMs = static_cast<std::uint64_t>(juce::Time::currentTimeMillis());
        params.sessionHeaderPath = logDirectory + "/session.header.txt";

        const double sampleRate = bus_.sampleRate();
        const std::uint32_t blockSamples = blockSamplesFor(config.blockSeconds, sampleRate);
        for (const int ch : channels) {
            if (ch < 0 || static_cast<std::size_t>(ch) >= SplSession::kMaxLoggedChannels) continue;
            rta::splexport::SplLogChannelSpec spec;
            spec.channel = ch;
            spec.basePath = logDirectory + "/ch" + std::to_string(ch);
            // A-WEIGHTED, ALWAYS -- not "whichever chain happens to be
            // first" (SplSession::window()'s own convention, which is Z by
            // default). Dose and Ln are both defined in dBA and both always
            // configured (SplSession::start's own comment: "an A-weighted
            // chain always exists"), and record §10's own worked example
            // shows `weighting=A` -- so the archival log records the ONE
            // chain that is guaranteed to exist and to match what the rest
            // of this session's own numbers (dose, Ln) are computed from.
            spec.info.weighting = rta::dsp::WeightingType::A;
            spec.info.detector = rta::meter::TimeWeighting::Fast;
            spec.info.blockSamples = blockSamples;
            spec.info.sampleRate = sampleRate;
            spec.info.startedAtUnixMs = params.startedAtUnixMs;
            // Task W2-E2b part A: absent for an uncalibrated first log,
            // present once a calibration START check has restarted this log
            // with an offset -- see `enableSplLogging`'s own comment.
            spec.info.calibratorLevelDb = calibratorLevelDb;
            params.channels.push_back(std::move(spec));
        }
        splState_.logPipeline.enable(params);
    } else {
        splState_.logPipeline.disable();
    }

    // Reset LAST, after the pipeline transition above has fully settled --
    // see that block's own comment for why the order matters. `splState_.blockCounts`
    // reading back to 0 is what every caller (production and test alike)
    // treats as "the request has landed".
    for (auto& count : splState_.blockCounts) count.store(0, std::memory_order_relaxed);
    for (auto& flags : splState_.flagsSeen) flags.store(0, std::memory_order_relaxed);
    for (auto& dropped : splState_.droppedSamples) dropped.store(0, std::memory_order_relaxed);
    for (auto& dropped : splState_.logDroppedBlocks) dropped.store(0, std::memory_order_relaxed);
    // Station-4 fix round (PR #31, round 3, LOW finding 4): this mirror was
    // missing from the reset above -- a channel whose PREVIOUS session ever
    // failed to write kept reporting splLogWriteFailed()==true forever after
    // a disable(), because nothing refreshes this mirror once feedSpl() stops
    // being called for that channel. Same shape as splState_.logDroppedBlocks just
    // above.
    for (auto& failed : splState_.logWriteFailed) failed.store(false, std::memory_order_relaxed);
    // Task W2-E2b part A: a fresh log (this function running at all) starts
    // an unverified calibration state again -- any PREVIOUS log's failed
    // verdict must not survive into a session that has not been calibrated
    // yet, the same "never inherits a previous session's state" reasoning
    // every other mirror in this function already follows.
    for (auto& invalid : splState_.calibrationInvalid) invalid.store(false, std::memory_order_relaxed);
}

void AnalysisThread::feedSpl(int channel) {
    // Station-4 fix round (PR #31, round 3, finding 1, MEDIUM: the epoch
    // race). A device reconfiguration bumps `lastEpoch_` (via
    // rebuildAnalysersIfEpochChanged(), always run before this on the same
    // tick) well before MainComponentSpl.cpp's 2 Hz poll notices and calls
    // enableSplLogging again -- up to 500 ms in which this session's own
    // SplSession/SplChannelState/SplLogPipeline are still sized and clocked
    // for the OLD rate. Freezing here, on the very first feedSpl() call
    // after the epoch changes, is what stops a mixed-rate block from ever
    // closing; there is no Gap to flag it otherwise (CaptureBus::prepare()
    // zeroes the drop counter). The freeze lifts the moment
    // applyPendingSplRequest() next runs with a real request -- disable then
    // EnableFresh, from the poll -- and re-captures splState_.sessionEpoch.
    if (splState_.sessionEpoch != lastEpoch_) return;
    if (!splState_.session.logsChannel(channel)) return;

    // The bus's own cumulative drop count, read BEFORE the hop is fed, so a
    // loss that happened before these samples rides the block they land in.
    // `CaptureBus::dropCount` is a relaxed atomic the callback increments
    // (platform/types/src/CaptureBus.cpp) -- reading it here is the whole of
    // this thread's knowledge that time went missing.
    splState_.session.noteDropCount(channel, bus_.dropCount(channel));
    splState_.session.feedHop(channel, channelScratch_[static_cast<std::size_t>(channel)]);

    // W2-E1: fold every CONFIGURED CHAIN's newly closed block into this
    // channel's own SPL state -- history, alarms, dose, Ln -- once each,
    // here, never recomputed at publish time and never read off a
    // (throttled) Snapshot. `AnalysisPublish.cpp`'s publish fold only READS
    // this state, at whatever rate `publishIfDue` runs.
    //
    // EVERY CHAIN, not just the first (fix round 2026-09-25, verifier HIGH
    // finding): `SplChannelState::onBlockClosed` routes each consumer
    // (alarm/dose/Ln) to the specific weighting its own definition names,
    // so it needs every chain's own closed block and window for this hop,
    // not one shared pair.
    const auto slot = static_cast<std::size_t>(channel);
    if (auto& state = splState_.channelStates[slot]) {
        const auto weightings = splState_.session.weightings();
        // At most one chain per WeightingType (A, C, Z) -- SplSession's own
        // bound (weightings_'s class comment).
        std::array<std::span<const rta::meter::Block>, 3> closedByChain{};
        std::array<std::span<const rta::meter::Block>, 3> fullWindowByChain{};
        std::size_t chainCount = std::min(weightings.size(), closedByChain.size());
        std::size_t closedCount = 0;
        for (std::size_t c = 0; c < chainCount; ++c) {
            closedByChain[c] = splState_.session.newlyClosedBlocks(channel, weightings[c]);
            fullWindowByChain[c] = splState_.session.window(channel, weightings[c]);
            closedCount = closedByChain[c].size();  // every chain closes in lockstep
        }

        std::array<ChainBlockAtClose, 3> atClose{};
        for (std::size_t i = 0; i < closedCount; ++i) {
            for (std::size_t c = 0; c < chainCount; ++c) {
                atClose[c].weighting = weightings[c];
                atClose[c].block = closedByChain[c][i];
                atClose[c].windowThroughThisBlock =
                    windowAtClose(closedByChain[c], fullWindowByChain[c], i);
            }
            state->onBlockClosed(std::span<const ChainBlockAtClose>(atClose.data(), chainCount));
        }

        // Record §5: Ln is sampled at the detector's own 100 ms clock, NEVER
        // tied to block closure -- fed once per hop, unconditionally,
        // whether or not the loop above closed any block this hop (fix
        // round 2026-09-25).
        state->feedLnTicks(splState_.session.newlyTickedLnLevelsDb(channel, rta::dsp::WeightingType::A));
    }

    // W2-E2a: the log-writing pipeline gets the A-weighted chain's newly
    // closed blocks -- the SAME chain applyPendingSplRequest() stamps into
    // the log header (see that function's own comment for why A, not
    // "whichever chain is first"). `newlyClosedBlocks` was already computed
    // by `feedHop` above, so this is a second read of the same per-chain
    // buffer, not a second pass over the samples.
    for (const auto& block : splState_.session.newlyClosedBlocks(channel, rta::dsp::WeightingType::A)) {
        splState_.logPipeline.pushBlock(channel, block);
    }
    if (slot < splState_.logDroppedBlocks.size()) {
        splState_.logDroppedBlocks[slot].store(splState_.logPipeline.droppedBlocks(channel),
                                         std::memory_order_relaxed);
    }
    // Station-4 fix round (PR #31, finding 6): same mirror as the drop count
    // just above, over splState_.logPipeline.writeFailed(channel).
    if (slot < splState_.logWriteFailed.size()) {
        splState_.logWriteFailed[slot].store(splState_.logPipeline.writeFailed(channel),
                                       std::memory_order_relaxed);
    }

    if (slot < splState_.blockCounts.size()) {
        splState_.blockCounts[slot].store(splState_.session.blockCount(channel), std::memory_order_relaxed);
        splState_.flagsSeen[slot].store(splState_.session.flagsSeen(channel), std::memory_order_relaxed);
        splState_.droppedSamples[slot].store(splState_.session.droppedSamplesTotal(channel),
                                       std::memory_order_relaxed);
    }
}

void AnalysisThread::enableSplLogging(const SplConfig& config, std::span<const int> channels,
                                      std::string logDirectory,
                                      std::optional<double> calibratorLevelDb) {
    {
        const std::lock_guard<std::mutex> lock(splState_.requestLock);
        splState_.requestConfig = config;
        splState_.requestChannels.assign(channels.begin(), channels.end());
        splState_.requestEnable = true;
        splState_.requestLogDirectory = std::move(logDirectory);
        splState_.requestCalibratorLevelDb = calibratorLevelDb;
    }
    splState_.requestPending.store(true, std::memory_order_release);
}

void AnalysisThread::disableSplLogging() {
    {
        const std::lock_guard<std::mutex> lock(splState_.requestLock);
        splState_.requestEnable = false;
        splState_.requestChannels.clear();
        splState_.requestLogDirectory.clear();
        splState_.requestCalibratorLevelDb.reset();
    }
    splState_.requestPending.store(true, std::memory_order_release);
}

void AnalysisThread::setCalibrationInvalid(int channel, bool invalid) noexcept {
    if (channel < 0 || static_cast<std::size_t>(channel) >= splState_.calibrationInvalid.size()) return;
    splState_.calibrationInvalid[static_cast<std::size_t>(channel)].store(invalid, std::memory_order_relaxed);
}

bool AnalysisThread::calibrationInvalid(int channel) const noexcept {
    if (channel < 0 || static_cast<std::size_t>(channel) >= splState_.calibrationInvalid.size()) return false;
    return splState_.calibrationInvalid[static_cast<std::size_t>(channel)].load(std::memory_order_relaxed);
}

std::uint64_t AnalysisThread::splBlockCount(int channel) const noexcept {
    if (channel < 0 || static_cast<std::size_t>(channel) >= splState_.blockCounts.size()) return 0;
    return splState_.blockCounts[static_cast<std::size_t>(channel)].load(std::memory_order_relaxed);
}

std::uint32_t AnalysisThread::splFlagsSeen(int channel) const noexcept {
    if (channel < 0 || static_cast<std::size_t>(channel) >= splState_.flagsSeen.size()) return 0;
    return splState_.flagsSeen[static_cast<std::size_t>(channel)].load(std::memory_order_relaxed);
}

void AnalysisThread::fillSplPublishInput(
    SplPublishInput& input, std::span<std::span<const rta::meter::Block>> metricWindows) const {
    // `config == nullptr` is the ONE way to say nothing is logging (W0-C C1),
    // so it is left alone on every path that does not find a completed block.
    const SplConfig* config = splState_.session.config();
    if (config == nullptr) return;

    // Wave 0 publishes the FIRST logged channel. One SPL block per Snapshot is
    // what `Snapshot::spl` is (record §9's payload is per-channel and the pane
    // is W2-D); a multi-channel publish is Wave 2's, and picking the lowest
    // logged channel is a rule a reader can state rather than whichever one
    // the container happened to order first.
    int channel = -1;
    for (int ch = 0; ch < static_cast<int>(SplSession::kMaxLoggedChannels); ++ch) {
        if (splState_.session.logsChannel(ch)) {
            channel = ch;
            break;
        }
    }
    if (channel < 0) return;

    auto latest = splState_.session.latestBlock(channel);
    if (!latest.has_value()) return;  // a session with no closed block yet

    input.config = config;
    input.sampleRate = splState_.session.sampleRate();
    input.latestBlock = latest;
    input.window = splState_.session.window(channel);
    // PER-METRIC windows: metric i's weighting decides which chain it is
    // averaged over, and without this an A-weighted and a C-weighted metric
    // would read the same numbers.
    const std::size_t filled = splState_.session.fillMetricWindows(channel, metricWindows);
    input.metricWindows = metricWindows.first(filled);

    // LOW follow-up batch, item 3: every SCALAR/mirror field below this point
    // (refusedMetrics, the round-4 overflow/floor pair, channelState, and the
    // three message-thread mirrors) is filled by a pure helper in
    // AnalysisPublish.cpp -- see that function's own comment for why the
    // fields COULD NOT be independently proven at this boundary before: this
    // whole file needs JUCE (through AnalysisThread.h), so it never runs in
    // the RTA_BUILD_APP=OFF target CI actually exercises on all three
    // operating systems, and a dropped assignment here compiled clean.
    //   - refusedMetrics: PR #17 verifier defect 1, carried straight to
    //     SplBlockView::refusedMetrics so a truncated metric list is visible.
    //   - overflowedLnTicks/blockSecondsTooSmall: round-4 items 3/4 -- the
    //     A-weighted chain's own tick-overflow count and the session's
    //     advisory blockSeconds-floor flag, neither of which SplChannelState
    //     ever sees.
    //   - channelState: W2-E1 -- the alarm/dose/Ln half of the publish reads
    //     from this channel's own accumulated state, never recomputed from
    //     `input.window` (SplAlarms/Dose/LevelHistogram all carry state
    //     across blocks that a per-publish recompute could not reconstruct).
    //   - logDroppedBlocks/logWriteFailed: W2-E2a / station-4 finding 6 --
    //     mirrors refreshed every hop from splState_.logPipeline itself (analysis-
    //     thread-only), read here rather than touching that object directly,
    //     keeping this function's own "no meter, no ring, no thread" contract
    //     (SplPublishInput's class comment) intact.
    //   - calibrationInvalid: task W2-E2b part A -- the calibration verdict
    //     is decided on the message thread (MainComponentCalibration.cpp),
    //     mirrored the same way.
    fillSplPublishScalars(input, splState_.session.refusedMetrics(),
                          splState_.session.overflowedLnTicks(channel, rta::dsp::WeightingType::A),
                          splState_.session.blockSecondsTooSmall(),
                          splState_.channelStates[static_cast<std::size_t>(channel)].get(),
                          splLogDroppedBlocks(channel), splLogWriteFailed(channel),
                          calibrationInvalid(channel));
}

std::uint64_t AnalysisThread::splLogDroppedBlocks(int channel) const noexcept {
    if (channel < 0 || static_cast<std::size_t>(channel) >= splState_.logDroppedBlocks.size()) return 0;
    return splState_.logDroppedBlocks[static_cast<std::size_t>(channel)].load(std::memory_order_relaxed);
}

bool AnalysisThread::splLogWriteFailed(int channel) const noexcept {
    if (channel < 0 || static_cast<std::size_t>(channel) >= splState_.logWriteFailed.size()) return false;
    return splState_.logWriteFailed[static_cast<std::size_t>(channel)].load(std::memory_order_relaxed);
}

std::uint64_t AnalysisThread::splDroppedSamples(int channel) const noexcept {
    if (channel < 0 || static_cast<std::size_t>(channel) >= splState_.droppedSamples.size()) return 0;
    return splState_.droppedSamples[static_cast<std::size_t>(channel)].load(std::memory_order_relaxed);
}

}  // namespace rta::measure
