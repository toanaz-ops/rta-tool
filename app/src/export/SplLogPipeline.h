// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
// Lane L6a task W2-E2a (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md
// "W2-E -- the wiring nobody was assigned"; record docs/dsp/
// 2026-09-16-spl-pro-l6a.md §10, §13 Q7).
//
// THE THREAD BOUNDARY this class exists to hold. The analysis thread closes
// a `rta::meter::Block` on the same thread that runs every other live
// measurement (repo CLAUDE.md's real-time rule: no allocation, no locks, no
// file I/O in that path), so it must never open a file itself. The message
// thread cannot write the log either: `Snapshot::spl` carries only the
// LATEST block (record §9), so a UI stall longer than one block period would
// silently lose every block in between -- there is no "catch up" once the
// snapshot has moved on. So a THIRD thread, owned by this class, drains a
// fixed-capacity single-producer/single-consumer queue -- one per logged
// channel -- into `SplLogWriter`.
//
// THE PRODUCER NEVER BLOCKS. `pushBlock()` is called from the analysis
// thread, at the exact point `AnalysisThread::feedSpl` already folds a
// closed block into `SplChannelState` (AnalysisThreadSpl.cpp) -- allocation-
// free after `enable()`, proven by app/tests/test_spl_log_pipeline.cpp's
// shared `AllocationProbe` (non-elidable: the dropped-block counter is read
// back and asserted on). A full queue means the writer thread -- disk I/O --
// cannot keep up; the block is counted as DROPPED-FROM-LOG and published
// (`AnalysisThread::splLogDroppedBlocks`), rather than the producer waiting
// for room, because waiting is exactly the real-time hazard this design
// exists to avoid.
//
// `rta::dsp::RingBuffer<rta::meter::Block>` is REUSED, not reimplemented:
// core's ring is already a generic lock-free SPSC queue over any trivial,
// default-constructible element, and `rta::meter::Block` is exactly that
// (Block.h's own static_asserts pin its layout).
#pragma once

#include "export/SplLog.h"
#include "export/SplSessionHeader.h"
#include "measure/SplConfig.h"
#include "measure/SplSession.h"  // kMaxLoggedChannels only -- no other dependency

#include "rta/dsp/RingBuffer.h"
#include "rta/dsp/Weighting.h"
#include "rta/meter/Block.h"
#include "rta/meter/Detector.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace rta::splexport {

/// One logged channel's own basename and header facts -- everything
/// `SplLogWriter` needs to open its first segment. The caller (`AnalysisThread`,
/// fed from `MainComponentSpl.cpp`) builds one of these per logged channel;
/// `basePath` is typically `<sessionDir>/ch<N>` (SplLog.h's own convention:
/// "directory + filename stem, no extension").
struct SplLogChannelSpec {
    int channel = -1;
    std::string basePath;
    SplLogHeaderInfo info;
};

/// Everything one `enable()` call needs, bundled so the signature does not
/// grow a new positional parameter every time this pipeline learns a new
/// fact about the session.
struct SplLogEnableParams {
    /// Read once, at `enable()` -- passed straight to every `SplLogWriter`
    /// (its own `referenceOffsetDb`/`calibrated`/`segmentBlocks`). Record
    /// §10: settings never change mid-log, so this is never re-read.
    rta::measure::SplConfig config;
    std::vector<SplLogChannelSpec> channels;
    /// Where the session-wide header (`SplSessionHeaderInfo`) is written --
    /// empty means none is written, which a caller wanting only the
    /// per-channel logs (e.g. a single-channel test fixture) can leave unset.
    std::string sessionHeaderPath;
    std::uint64_t startedAtUnixMs = 0;
    /// Rounded up to a power of two by `rta::dsp::RingBuffer`. The producer
    /// counts a block as dropped rather than waiting once this many blocks
    /// are unwritten -- see this file's own header comment.
    std::size_t queueCapacityBlocks = 256;
};

/// The log-writing half of W2-E2a: a fixed-capacity SPSC `rta::meter::Block`
/// queue per logged channel, allocated at `enable()` and never grown, plus a
/// single dedicated writer thread that drains every channel's queue into its
/// own `SplLogWriter`.
///
/// THREADING. `enable()`/`disable()` are called from
/// `AnalysisThread::applyPendingSplRequest()` -- the analysis thread's own
/// deferred-request handover, the SAME moment `SplSession::start()`/`stop()`
/// run (that class's own class comment: "the caller is responsible for
/// handing them over"). `pushBlock()` is called from the SAME thread, inside
/// `feedSpl()`. So `sinks_` has exactly one writer (the analysis thread) and
/// one reader (the writer thread this class owns), and `disable()` always
/// JOINS the writer thread BEFORE touching `sinks_` again -- no lock is
/// needed for the array itself, only `rta::dsp::RingBuffer`'s own internal
/// atomics guard the data actually crossing the thread boundary.
class SplLogPipeline {
public:
    SplLogPipeline() = default;
    ~SplLogPipeline() { disable(); }

    SplLogPipeline(const SplLogPipeline&) = delete;
    SplLogPipeline& operator=(const SplLogPipeline&) = delete;

    /// Stops and replaces any previous session (a fresh log, never appended
    /// to -- record §10). Allocates one ring buffer per entry of
    /// `params.channels` and starts the writer thread -- NO FILE I/O runs on
    /// the calling thread (station-4 fix round, PR #31, verifier finding 4:
    /// opening every first segment and writing the session header used to
    /// happen HERE, on whatever thread called `enable()` -- the analysis
    /// thread in production, which repo CLAUDE.md's real-time rule forbids
    /// doing file I/O on). Every bit of that file I/O now runs as the
    /// writer thread's own first act, in `writerLoop()`, before it starts
    /// draining -- see that function. `pushBlock()` only ever touches the
    /// ring, which this call DOES allocate on the calling thread (the same
    /// "runs once per session start, never once per block" reasoning
    /// `SplSession::start()` already relies on), so a block pushed the
    /// instant `enable()` returns has somewhere to land even before the
    /// writer thread finishes opening files.
    void enable(const SplLogEnableParams& params);

    /// Signals the writer thread to drain everything already pushed, then
    /// joins it -- "shutdown drains then joins" (W2-E2a's own acceptance).
    /// Safe to call when already disabled (a no-op). The join's wait is
    /// bounded in WORK, not wall-clock: one final `drainOnce()` pass over
    /// data already queued (at most `queueCapacityBlocks` per channel,
    /// fixed at `enable()`) plus the OS closing already-open file handles --
    /// neither can grow without bound. `disable()` never runs on the
    /// real-time audio callback (that callback only ever touches `AudioIo`,
    /// never this class), so blocking the analysis thread here costs it one
    /// late `publishIfDue()` tick, not a dropout.
    void disable() noexcept;

    /// Test-only injection point (station-4 fix round, PR #31, finding 4):
    /// replaces how `writerLoop()` constructs each channel's `SplLogWriter`.
    /// A production caller never sets this -- the default factory just
    /// constructs a real one. `app/tests/test_spl_log_pipeline.cpp` uses it
    /// to record `std::this_thread::get_id()` at construction time and
    /// prove that id is the WRITER thread's, never the thread that called
    /// `enable()`.
    using WriterFactory =
        std::function<std::unique_ptr<SplLogWriter>(const std::string& basePath,
                                                     const rta::measure::SplConfig& config,
                                                     const SplLogHeaderInfo& info,
                                                     std::uint64_t segmentBlocks)>;
    void setWriterFactoryForTest(WriterFactory factory) { writerFactory_ = std::move(factory); }

    [[nodiscard]] bool enabled() const noexcept { return running_.load(std::memory_order_acquire); }

    /// Analysis-thread call. NEVER BLOCKS, NEVER ALLOCATES (measured by
    /// app/tests/test_spl_log_pipeline.cpp). A channel this pipeline has no
    /// sink for (not passed to `enable`, or `enable` never called) is
    /// silently a no-op: the caller only invokes this for a channel
    /// `SplSession` is already logging, so reaching here with no sink is a
    /// wiring question for the caller, not a drop this class should count.
    void pushBlock(int channel, const rta::meter::Block& block) noexcept;

    /// Blocks pushed for `channel` that the queue was too full to accept.
    /// Safe from any thread: an atomic counter the analysis thread alone
    /// increments (`pushBlock`) and any thread may read.
    [[nodiscard]] std::uint64_t droppedBlocks(int channel) const noexcept;

    /// Station-4 fix round (PR #31, verifier finding 6, MEDIUM): true once
    /// `channel`'s `SplLogWriter` has ever failed to open a segment -- sticky,
    /// same reasoning as `SplLogWriter::openFailed()`'s own comment. Safe
    /// from any thread: mirrors that writer's own state into an atomic the
    /// writer thread alone sets (`setupWriters()`, `drainOnce()`) and any
    /// thread may read, the same shape as `droppedBlocks()` above.
    [[nodiscard]] bool writeFailed(int channel) const noexcept;

private:
    using Ring = rta::dsp::RingBuffer<rta::meter::Block>;

    struct ChannelSink {
        int channel = -1;
        std::unique_ptr<Ring> ring;
        std::unique_ptr<SplLogWriter> writer;
        std::atomic<std::uint64_t> dropped{ 0 };
        std::atomic<bool> writeFailed{ false };
    };

    /// One drain pass over every sink, writing whatever each ring currently
    /// holds. @return true if any block was written, so `writerLoop` knows
    /// whether to sleep before the next pass.
    bool drainOnce();
    void writerLoop();

    /// The writer thread's own first act, called once from the top of
    /// `writerLoop()`, before the drain loop starts: opens every channel's
    /// first segment (via `writerFactory_`) and the session header, if
    /// requested -- all the file I/O `enable()` used to do on the calling
    /// thread (finding 4). Reads `pendingParams_`, which `enable()` wrote on
    /// the calling thread strictly before starting this thread, so no lock
    /// is needed (`std::thread`'s constructor synchronizes-with the start of
    /// the new thread's execution).
    void setupWriters();

    static constexpr std::size_t kMaxLoggedChannels = rta::measure::SplSession::kMaxLoggedChannels;

    std::array<std::unique_ptr<ChannelSink>, kMaxLoggedChannels> sinks_{};
    std::thread writerThread_;
    std::atomic<bool> running_{ false };
    /// Written by `enable()` on the calling thread, read only by
    /// `setupWriters()` on the writer thread -- see that function's own
    /// comment for why this needs no lock.
    SplLogEnableParams pendingParams_;
    WriterFactory writerFactory_ = [](const std::string& basePath,
                                      const rta::measure::SplConfig& config,
                                      const SplLogHeaderInfo& info,
                                      std::uint64_t segmentBlocks) {
        return std::make_unique<SplLogWriter>(basePath, config, info, segmentBlocks);
    };
    // `disable()` destroys every ChannelSink (and with it, `dropped`) once the
    // writer thread has joined -- but a caller's whole reason to read
    // `droppedBlocks()` is often "how many did the session that just ended
    // lose", the same shape as `AnalysisThread::splLogDroppedBlocks` snapshot
    // in a Snapshot taken after the bus goes inactive. So `writerLoop()`'s own
    // shutdown sequence copies each sink's final count here BEFORE resetting
    // it, and `droppedBlocks()` falls back to this snapshot once the live
    // sink is gone. `enable()` zeroes it for the new session -- a fresh log
    // never inherits a previous session's drop count (record §10: never
    // appended to).
    std::array<std::atomic<std::uint64_t>, kMaxLoggedChannels> lastDropped_{};
    /// Same snapshot shape as `lastDropped_`, for `writeFailed()` (finding 6).
    std::array<std::atomic<bool>, kMaxLoggedChannels> lastWriteFailed_{};
};

}  // namespace rta::splexport
