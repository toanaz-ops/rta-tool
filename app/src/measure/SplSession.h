// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Lane L6a task W0-D (research §C4, SPL-R1, SPL-R2).
#pragma once

#include "measure/SplConfig.h"
#include "measure/SplMeter.h"

#include "rta/meter/Block.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace rta::measure {

/// One live SPL logging session: an `SplMeter` per logged channel, the window
/// each metric is recomputed over, and the bus drop-count baseline that turns
/// a stalled drain into a `Gap` WITH A LENGTH.
///
/// WHY THIS IS ITS OWN FILE (deviation from the plan's file list, named).
/// W0-D's task lists `AnalysisThread.h/.cpp` only, with the escape hatch "if
/// it breaches, the seam is drain-versus-feed and the feed moves out". It
/// does breach: `AnalysisThread.cpp` is 365 lines against a 400 hard cap and
/// the session state is about 50. So the feed moved, and it moved HERE rather
/// than into `AnalysisPublish.cpp` because this is STATE with a lifetime,
/// while that file is free functions over objects the caller owns. The win is
/// the same one the plan wanted and one more besides: this file is JUCE-free,
/// so the whole session -- the block clock, the gap arithmetic, the window --
/// is provable with `RTA_BUILD_APP=OFF` on all three CI operating systems,
/// and `AnalysisThread` keeps only the three lines that are genuinely about
/// the drain.
///
/// THREADING. Analysis-thread-only, and not synchronised: `start`/`stop`
/// allocate and are the message thread's, so the caller is responsible for
/// handing them over (`AnalysisThread` does it with the same
/// atomic-request-flag shape `armLocateCapture` already uses). Every accessor
/// below returns a plain value read on the analysis thread; publishing them
/// across a thread boundary is the caller's job, exactly as `routeHopCounts_`
/// already is.
///
/// NOT A SECOND RING READER. `feedHop` takes the scratch buffer the drain
/// already filled. `rta::dsp::RingBuffer` has ONE `readIndex_` and
/// `AnalysisThread::drainPaired` owns it (record §0 hazard 2); a second
/// cursor is a `platform/` change and is explicitly not built here.
class SplSession {
public:
    /// Channels are indexed directly, like `AnalysisThread::channelScratch_`.
    /// The SPL meters are NOT `analysers_` and are NOT bounded by
    /// `kMaxTransferFunctions` -- that constant is an MTW-memory policy
    /// (RoutingPlan.h:25-29), not a channel-count limit, and a route past it
    /// must still log (W0-D D1b).
    static constexpr std::size_t kMaxLoggedChannels = 64;

    /// Wave 0's window cap. The real ring, sized once from
    /// `SplConfig::logSpanSeconds` and never grown, is W2-A; until then a
    /// window is capped at the longest metric's own `windowBlocks` and at
    /// this, so a long session cannot grow without bound.
    static constexpr std::size_t kMaxWindowBlocks = 3600;

    SplSession() = default;

    /// Begins a session on `channels`. ALLOCATES -- the meters and the
    /// windows are built here and nothing after. A channel outside
    /// [0, kMaxLoggedChannels) is ignored rather than clamped onto a
    /// neighbour's slot.
    void start(const SplConfig& config, double sampleRate, std::span<const int> channels);

    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] bool logsChannel(int channel) const noexcept;

    /// Records the bus's own CUMULATIVE dropped-sample count for `channel`
    /// (`rta::platform::CaptureBus::dropCount`). The DELTA since the last
    /// call rides the block being accumulated as `droppedSamples` and sets
    /// `BlockFlag::Gap` on it.
    ///
    /// Taken as a plain number rather than a `CaptureBus&` so this file needs
    /// no platform header and stays provable with no bus in the path.
    ///
    /// The FIRST call on a channel only establishes the baseline: a bus that
    /// has already dropped samples before logging began has not lost anything
    /// from THIS log.
    void noteDropCount(int channel, std::uint64_t busDropCount) noexcept;

    /// Feeds one hop from the scratch the drain already filled, then drains
    /// every block that completed into `channel`'s window. Allocation-free.
    void feedHop(int channel, std::span<const float> hop) noexcept;

    [[nodiscard]] std::uint64_t blockCount(int channel) const noexcept;
    /// The OR of every flag any block on this channel has carried. Monotonic,
    /// so a later clean block cannot erase a Gap that happened.
    [[nodiscard]] std::uint32_t flagsSeen(int channel) const noexcept;
    [[nodiscard]] std::uint64_t droppedSamplesTotal(int channel) const noexcept;

    [[nodiscard]] std::optional<rta::meter::Block> latestBlock(int channel) const noexcept;
    /// `channel`'s window, oldest first and contiguous, or an empty span.
    [[nodiscard]] std::span<const rta::meter::Block> window(int channel) const noexcept;

    [[nodiscard]] const SplConfig* config() const noexcept {
        return running_ ? &config_ : nullptr;
    }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }

private:
    struct ChannelState {
        ChannelState(const SplConfig& config, double sampleRate, std::size_t windowCapacity);

        SplMeter meter;
        std::vector<rta::meter::Block> window;  ///< oldest first, capacity fixed at start()
        std::uint64_t blocks = 0;
        std::uint32_t flagsSeen = 0;
        std::uint64_t droppedSamplesTotal = 0;
        std::uint64_t lastBusDropCount = 0;
        bool dropBaselineSet = false;
    };

    [[nodiscard]] ChannelState* state(int channel) noexcept;
    [[nodiscard]] const ChannelState* state(int channel) const noexcept;

    SplConfig config_;
    double sampleRate_ = 0.0;
    bool running_ = false;
    std::size_t windowCapacity_ = 1;
    std::array<std::unique_ptr<ChannelState>, kMaxLoggedChannels> channels_;
};

}  // namespace rta::measure
