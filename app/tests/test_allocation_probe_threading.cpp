// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The PROBE's per-thread ATTRIBUTION case (round 4, MEDIUM finding 2; LOW
// follow-up batch, item 13's aligned-overload extension), split out of
// test_allocation_probe.cpp (436 lines, over the 400-line hard cap) -- "does
// the counter attribute correctly across two concurrent threads" is a
// different subject from B0b/B0c/B0d's single-threaded anchors, and this is
// the one case in the file that spawns a background thread at all.
//
// Own copies of the small `g_opaqueCount`/`g_sink`/`OverAligned` fixtures
// test_allocation_probe.cpp's anonymous namespace also declares -- each is
// under ten lines and an anonymous namespace cannot be shared across TUs, the
// same trade-off test_spl_log_pipeline.cpp's own multi-file split already
// makes for its `TempDir`/`channelSpec`/`distinguishableBlock` fixtures.
#include <catch2/catch_test_macros.hpp>

#include "AllocationProbe.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>
#include <vector>

namespace {

/// See test_allocation_probe.cpp's own `g_opaqueCount`/`g_sink` for the full
/// clang-elision argument this defends against -- both are load-bearing for
/// the same reason there.
volatile std::size_t g_opaqueCount = 1024;
volatile double g_sink = 0.0;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
struct alignas(64) OverAligned {
    double value;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace

// --- round 4, MEDIUM finding 2: per-thread attribution --------------------

TEST_CASE("AllocationProbe only charges the thread that armed it",
          "[allocationprobe]") {
    // CI-discovered regression, round 3 (PR #31): SplLogPipeline's writer
    // thread does its own legitimate allocating (opening a file, a log
    // row's std::string) concurrently with whatever a test's own probe is
    // measuring on the MAIN thread. Before this fix, g_countingActive was
    // the only gate -- one flag every thread's operator new checked -- so a
    // background thread's allocation landed in the SAME counter, caught
    // intermittently on ubuntu-latest (32 bytes over 640 pushBlock calls)
    // and nowhere else, because it depends on the OS scheduler's own timing.
    rta::test::resetAllocationProbe();

    // A background thread doing REAL, escaping allocation for the entire
    // life of this test, with NO synchronization against the probe scopes
    // below -- as adversarial as this can get without an actual writer
    // thread. `backgroundSink`/`backgroundAlignedSink` are local and touched
    // ONLY by this thread (never `g_sink`, which the main thread's own
    // probes below write), so there is no data race between the two
    // threads' escapes.
    //
    // BOTH allocation paths, deliberately (LOW follow-up batch, item 13):
    // the unaligned `std::vector<double>` AND an `alignas(64)` `OverAligned`
    // vector, alternating -- AllocationProbe.cpp:151's aligned
    // `operator new(size, align_val_t)` has its OWN, separate per-thread
    // check, and a background thread that only ever exercised the UNALIGNED
    // overload could never catch that check being dropped there specifically.
    std::atomic<bool> stop{ false };
    volatile double backgroundSink = 0.0;
    volatile double backgroundAlignedSink = 0.0;
    std::thread background([&] {
        bool alignedTurn = false;
        while (!stop.load(std::memory_order_acquire)) {
            if (alignedTurn) {
                std::vector<OverAligned> alignedNoise(g_opaqueCount / 16 + 1);
                backgroundAlignedSink = alignedNoise.back().value;
            } else {
                std::vector<double> noise(g_opaqueCount, 1.0);
                backgroundSink = noise.back();
            }
            alignedTurn = !alignedTurn;
        }
    });

    // Half one of the mutant: a background thread allocating throughout must
    // NOT make an IDLE arming thread's probe read anything but zero. Remove
    // the per-thread check (this test's own mutant) and this goes red the
    // instant the background thread's timing lands inside the window --
    // observed directly on ubuntu-latest CI before this fix, not merely
    // theoretical. The background thread now alternates unaligned and aligned
    // allocations (see its own comment above), so this one check already
    // covers "an idle thread is not charged for a background thread's ALIGNED
    // allocations" too -- dropping the per-thread check inside the ALIGNED
    // overload specifically (AllocationProbe.cpp:151) makes this go red the
    // instant the background thread's aligned turn lands inside the window.
    std::size_t bytesWhileIdle = 0;
    {
        const rta::test::AllocationProbe probe;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        bytesWhileIdle = probe.bytes();
    }
    INFO("bytes charged to an IDLE arming thread while a background thread "
        << "allocated (both unaligned and aligned) concurrently = " << bytesWhileIdle);
    CHECK(bytesWhileIdle == 0);

    // Half two of the SAME mutant, checked with the background thread still
    // running: the arming thread's OWN allocation must still be counted.
    // This is what proves the fix is per-thread ATTRIBUTION, not a probe
    // that stopped counting anything at all.
    std::size_t bytesOwn = 0;
    {
        const rta::test::AllocationProbe probe;
        std::vector<double> mine(g_opaqueCount, 2.0);
        g_sink = mine.back();
        bytesOwn = probe.bytes();
    }
    INFO("bytes charged to the arming thread's OWN allocation = " << bytesOwn);
    CHECK(bytesOwn >= g_opaqueCount * sizeof(double));

    // The OWN-THREAD ALIGNED half (LOW follow-up batch, item 13): the same
    // "still counted, with the background thread still running" proof as
    // `bytesOwn` above, but through the ALIGNED overload -- so a mutation
    // that broke per-thread attribution in ONLY that overload, in a way that
    // also stopped it from counting the arming thread's own aligned
    // allocation (rather than merely over-counting an idle one), is caught
    // here rather than only by B0d's single-threaded case in
    // test_allocation_probe.cpp, which never runs a background thread at all.
    std::size_t bytesOwnAligned = 0;
    {
        const rta::test::AllocationProbe probe;
        std::vector<OverAligned> mineAligned(g_opaqueCount);
        g_sink = mineAligned.back().value;
        bytesOwnAligned = probe.bytes();
    }
    INFO("bytes charged to the arming thread's OWN aligned allocation = " << bytesOwnAligned);
    CHECK(bytesOwnAligned >= g_opaqueCount * sizeof(OverAligned));

    stop.store(true, std::memory_order_release);
    background.join();
}
