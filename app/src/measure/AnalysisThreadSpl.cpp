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

    // W2-E1: one SplChannelState per logged channel, allocated HERE --
    // exactly where splSession_.start() allocates its own chains and
    // windows -- and never resized afterward. A disable (or a fresh enable
    // replacing a previous session) drops every existing state, so a
    // channel's history/alarms/dose/Ln never survive across two sessions
    // that happen to share a channel number.
    for (auto& state : splChannelStates_) state.reset();
    if (enable) {
        for (const int ch : channels) {
            if (ch < 0 || static_cast<std::size_t>(ch) >= splChannelStates_.size()) continue;
            splChannelStates_[static_cast<std::size_t>(ch)] =
                std::make_unique<SplChannelState>(config, bus_.sampleRate());
        }
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
    if (auto& state = splChannelStates_[slot]) {
        const auto weightings = splSession_.weightings();
        // At most one chain per WeightingType (A, C, Z) -- SplSession's own
        // bound (weightings_'s class comment).
        std::array<std::span<const rta::meter::Block>, 3> closedByChain{};
        std::array<std::span<const rta::meter::Block>, 3> fullWindowByChain{};
        std::size_t chainCount = std::min(weightings.size(), closedByChain.size());
        std::size_t closedCount = 0;
        for (std::size_t c = 0; c < chainCount; ++c) {
            closedByChain[c] = splSession_.newlyClosedBlocks(channel, weightings[c]);
            fullWindowByChain[c] = splSession_.window(channel, weightings[c]);
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
    }

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
    // W2-E1: the alarm/dose/Ln half of the publish reads from this channel's
    // own accumulated state, never recomputed from `input.window` --
    // SplAlarms/Dose/LevelHistogram all carry state across blocks (a fired
    // alarm's sinceBlock, an accumulated dose) that a per-publish recompute
    // could not reconstruct.
    input.channelState = splChannelStates_[static_cast<std::size_t>(channel)].get();
}

std::uint64_t AnalysisThread::splDroppedSamples(int channel) const noexcept {
    if (channel < 0 || static_cast<std::size_t>(channel) >= splDroppedSamples_.size()) return 0;
    return splDroppedSamples_[static_cast<std::size_t>(channel)].load(std::memory_order_relaxed);
}

}  // namespace rta::measure
