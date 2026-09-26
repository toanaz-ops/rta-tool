// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The PROBE's own tests (L6a task W0-B0 cases B0b and B0c), split out of
// test_average_group.cpp along the seam that made that file long: "does the
// counting allocator work" is a different subject from "is the spatial
// average's publish churn O(1) in N", and the first one is what every later
// measuring file (test_spl_meter.cpp, test_spl_session.cpp) depends on.
//
// What the cases here are for:
//   B0b  the replaced global operator new is ONE DEFINITION PER PROGRAM, and
//        the linker succeeding is only half that proof.
//   B0c  the ANCHOR: a direct `::operator new` call no optimiser may remove,
//        so a reading of zero cannot be confused with an elided allocation.
//   B0c  the probe RESETS on construction, so one TEST_CASE cannot read
//        another's bytes.
//   B0d  the OVER-ALIGNED path, added when the C++17 aligned trio was
//        replaced: an aligned anchor, a `std::vector` of an over-aligned
//        element, and a heap-allocated `rta::dsp::RingBuffer`, the type that
//        motivated it all.
//
// WHICH PROCESS RUNS WHICH CASE, stated correctly because an earlier revision
// of this file stated it wrongly. `catch_discover_tests` registers ONE ctest
// test per Catch2 case and re-invokes the binary once per case with that
// case's name as a filter -- so under `ctest` every case gets a FRESH process
// and the program-global byte counter cannot carry across cases at all. The
// counter is shared only when the binary is run DIRECTLY, which is what a
// developer does (`rtatool_analysis_tests --order decl`, or a tag filter
// like `[allocationprobe]`), what every mutation check in this repo does, and
// what the two `allocation_probe_one_process_order_*` ctest entries in
// app/tests/CMakeLists.txt now do deliberately.
//
// That mattered: the pre-probe assertion below read the counter's RESIDUE
// from whichever case ran before it, so `--order decl` in one process failed
// `8192 == 0` while ctest stayed green. ctest was hiding an order dependence,
// not proving its absence.
#include <catch2/catch_test_macros.hpp>

#include "AllocationProbe.h"
#include "CodeLines.h"

#include "rta/dsp/RingBuffer.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace {

/// An element count the optimiser cannot fold, and a sink it cannot see
/// through. Both are load-bearing, and only on Apple clang.
///
/// libc++'s `std::allocator<T>::allocate` reaches the heap through
/// `__builtin_operator_new`, which clang documents as ELIDABLE: the compiler
/// may delete a matched new/delete pair whose pointer never escapes. At -O3
/// it does exactly that to a local `std::vector` that is written once and
/// never read -- so an earlier revision of B0c below measured a
/// `std::vector<double>(1024)` that WAS NEVER ALLOCATED, the probe correctly
/// counted zero bytes of nothing, and the case read `0 >= 8192` on
/// macos-latest (run 35306075307). libstdc++ and the MSVC STL call
/// `::operator new` as a plain function, which no optimiser is permitted to
/// remove, which is why the same case was green on the other two runners.
///
/// Neither of these is a trick played on the test. The property under test is
/// that the PROBE resets; the allocation has to actually happen for there to
/// be anything for it to measure, and a fixture whose subject the optimiser
/// deleted is the "too well-behaved to fail" failure in
/// memory/a-fixture-can-be-too-well-behaved-to-fail.md.
volatile std::size_t g_opaqueCount = 1024;

/// Reading one element back through a volatile is what makes the buffer
/// ESCAPE, and escaping is the condition clang's elision cannot satisfy.
volatile double g_sink = 0.0;

/// An element whose alignment forces the OVER-ALIGNED allocation path. The
/// static_assert is the point of the type: if a platform ever raised
/// `__STDCPP_DEFAULT_NEW_ALIGNMENT__` to 64, B0d's vector case would go on
/// passing while testing the unaligned path it was written to leave alone --
/// a fixture that cannot fail for the reason it claims.
///
/// MSVC warns the struct grew (C4324); that growth is the point, as it is
/// around RingBuffer's own index members.
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

static_assert(alignof(OverAligned) > __STDCPP_DEFAULT_NEW_ALIGNMENT__,
              "OverAligned must exceed the default new alignment, or the B0d vector case "
              "silently measures the unaligned path");

static_assert(alignof(rta::dsp::RingBuffer<float>) > __STDCPP_DEFAULT_NEW_ALIGNMENT__,
              "RingBuffer must be over-aligned, or B0d's motivating case proves nothing");

}  // namespace

// --- W0-B0: the probe is one definition, and the linker is not the only
// --- thing that says so ------------------------------------------------

TEST_CASE("B0b the replaced global operator new is defined in exactly one file under app tests",
          "[allocationprobe]") {
    // A replaceable global allocation function is ONE DEFINITION PER PROGRAM.
    // The link succeeding is half the proof; this is the other half, because
    // a second definition added to a file not yet in this binary would link
    // fine today and break the day that file is added to the source list.
    const std::filesystem::path testsDir = std::filesystem::path(RTA_REPO_ROOT) / "app" / "tests";
    REQUIRE(std::filesystem::is_directory(testsDir));

    std::vector<std::string> definers;
    for (const auto& entry : std::filesystem::directory_iterator(testsDir)) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".h" && ext != ".hpp") continue;
        // codeText() lowercases, strips comments and empties literals, so a
        // sentence in a doc comment about operator new cannot match here.
        const std::string text = rta::test::codeText(entry.path());
        if (text.find("void* operator new(") != std::string::npos
            && text.find("void* operator new(std::size_t size);") == std::string::npos) {
            definers.push_back(entry.path().filename().string());
        }
    }
    for (const auto& f : definers) INFO("  defines operator new: " << f);
    INFO("count = " << definers.size());
    REQUIRE(definers.size() == 1);
    CHECK(definers.front() == "AllocationProbe.cpp");
}

TEST_CASE("B0c the replaced operator new is the one this binary calls", "[allocationprobe]") {
    // THE ANCHOR, and the reason it is a direct `::operator new` call rather
    // than a container: a direct call to a replaceable global allocation
    // function is not a new-expression and not `__builtin_operator_new`, so
    // NO optimiser on any of the three CI platforms may remove it. If the
    // replacement were not installed at all -- a linker picking the
    // implementation's own definition, say -- this case is the one that says
    // so, and it says it without the standard library or -O3 in the way.
    //
    // Without it, "the probe read zero" and "the allocation did not happen"
    // are indistinguishable, which is exactly the ambiguity the macos-latest
    // log of run 35306075307 left behind.
    //
    // No Catch2 macro runs inside the armed scope: an assertion's own
    // bookkeeping is an allocation this counter would charge to the
    // measurement, which is why `counted` is read before anything is checked.
    // Zero the PROGRAM-GLOBAL counter before reading it. `AllocationProbe`'s
    // constructor already does this, so the anchor does not need it -- it is
    // here so the two cases in this file establish their baseline the same
    // way and neither depends on which one Catch2 ran first.
    rta::test::resetAllocationProbe();

    const std::size_t bytes = g_opaqueCount * sizeof(double);
    std::size_t counted = 0;
    void* raw = nullptr;
    {
        const rta::test::AllocationProbe probe;
        raw = ::operator new(bytes);
        counted = probe.bytes();
    }
    ::operator delete(raw, bytes);
    INFO("::operator new(" << bytes << ") counted " << counted << " bytes");
    CHECK(raw != nullptr);
    CHECK(counted == bytes);
}

TEST_CASE("B0c AllocationProbe resets on construction so one case cannot read another's bytes",
          "[allocationprobe]") {
    // Every allocation below is a std::vector, deliberately: the measuring
    // cases this probe exists for (test_spl_meter.cpp B4, test_spl_session.cpp
    // feedHop, T12 in test_average_group.cpp) all measure container traffic,
    // so the path asserted here has to be the container path and not the
    // direct one the case above anchors. Each buffer escapes through
    // `g_sink` and is sized from `g_opaqueCount`; see that variable's comment
    // for the elision this defends against and the run that found it.
    const std::size_t count = g_opaqueCount;
    // `small` has to be at least one element: `b.back()` on an empty vector
    // is undefined, and the count comes from a mutable global.
    const std::size_t small = count / 64;
    REQUIRE(small >= 1);

    // ZERO THE COUNTER FIRST, and this line is the fix for a real order
    // dependence rather than defensive noise. The byte counter is
    // program-global and `~AllocationProbe` deliberately does NOT clear it --
    // `AllocationProbe.h` documents the reading as valid after the guard
    // leaves scope, which is what lets a helper return a measurement it took
    // inside one (`measureGroupPublishBytes` in test_average_group.cpp does
    // exactly that). So in a ONE-PROCESS run the previous case's total is
    // still sitting there, and the assertion below -- which runs before any
    // probe is constructed in this case -- was reading it: `--order decl`
    // failed `8192 == 0`, the anchor case's bytes.
    //
    // The claim being made is "counting is OFF outside a probe". Asserting it
    // against a KNOWN-ZERO baseline is that claim; asserting it against
    // whatever the last case left behind is a different, weaker one that also
    // happens to be false.
    rta::test::resetAllocationProbe();

    // Allocate OUTSIDE any probe: the counter must not be running at all.
    {
        std::vector<double> noise(count * 4, 1.0);
        g_sink = noise.back();
        CHECK(noise.size() == count * 4);
    }
    CHECK(rta::test::allocationBytes() == 0);

    std::size_t first = 0;
    {
        const rta::test::AllocationProbe probe;
        std::vector<double> a(count);
        a[0] = 1.0;
        g_sink = a.front() + a.back();
        first = probe.bytes();
    }
    INFO("first = " << first << " bytes for " << count << " doubles");
    CHECK(first >= count * sizeof(double));

    // A second probe sees its OWN allocations only: it resets on
    // construction, so `first` cannot leak into it. Not hypothetical -- a
    // direct one-process run puts three cases through this one counter, and
    // dropping the constructor's reset turns the assertion below red (the
    // mutation is in PR #22's body).
    std::size_t second = 0;
    {
        const rta::test::AllocationProbe probe;
        std::vector<double> b(small);
        b[0] = 1.0;
        g_sink = b.front() + b.back();
        second = probe.bytes();
    }
    INFO("first = " << first << ", second = " << second);
    CHECK(second >= small * sizeof(double));
    CHECK(second < first);

    // Counting is OFF once the guard leaves scope: the reading does not move.
    {
        std::vector<double> after(count * 4, 2.0);
        g_sink = after.back();
        CHECK(after.size() == count * 4);
    }
    CHECK(rta::test::allocationBytes() == second);
}

// --- W0-B0 B0d: the C++17 over-aligned set is replaced too ---------------

TEST_CASE("B0d the replaced aligned operator new is the one this binary calls",
          "[allocationprobe]") {
    // THE ALIGNED ANCHOR, direct for the same reason B0c's is: a direct call
    // to a replaceable global allocation function is not a new-expression and
    // not `__builtin_operator_new`, so no optimiser on any of the three CI
    // platforms may remove it. That is what splits "the aligned overload is
    // NOT REPLACED" from "the allocation was ELIDED" -- the same split B0c
    // makes for the unaligned path, and without it a zero here means either.
    // No Catch2 macro runs inside the armed scope: an assertion's own
    // bookkeeping allocates, and this counter would charge it here.
    const std::size_t bytes = g_opaqueCount * sizeof(double);
    std::size_t counted = 0;
    void* raw = nullptr;
    {
        const rta::test::AllocationProbe probe;
        raw = ::operator new(bytes, std::align_val_t{64});
        counted = probe.bytes();
    }
    ::operator delete(raw, bytes, std::align_val_t{64});
    INFO("::operator new(" << bytes << ", align_val_t{64}) counted " << counted << " bytes");
    CHECK(raw != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(raw) % 64 == 0);
    CHECK(counted == bytes);
}

TEST_CASE("B0d a std::vector of an over-aligned type is counted", "[allocationprobe]") {
    // The CONTAINER form of the aligned path, which is the form a shipped
    // caller takes. `std::allocator<T>::allocate` routes an over-aligned T to
    // the aligned new on all three standard libraries -- libc++ through
    // `__libcpp_allocate`, libstdc++ through `__new_allocator`, the MSVC STL
    // through `_Allocate` with `_New_alignof` -- so this is the route a
    // heap-allocated RingBuffer travels. Count from `g_opaqueCount`, one
    // element out through `g_sink`: those two variables document the -O3
    // elision this defends against, and a deleted subject makes the probe
    // read zero of nothing.
    const std::size_t count = g_opaqueCount;
    std::size_t counted = 0;
    {
        const rta::test::AllocationProbe probe;
        std::vector<OverAligned> v(count);
        v[0].value = 1.0;
        g_sink = v.front().value + v.back().value;
        counted = probe.bytes();
    }
    INFO("vector<OverAligned>(" << count << ") counted " << counted << " bytes, alignof = "
                                << alignof(OverAligned));
    CHECK(counted >= count * sizeof(OverAligned));
}

TEST_CASE("B0d a heap-allocated RingBuffer is counted", "[allocationprobe]") {
    // THE MOTIVATING TYPE. `rta::dsp::RingBuffer` puts its producer and
    // consumer indices on separate cache lines (`alignas(64)`), so every
    // heap-allocated one goes through the aligned new -- invisible to the
    // counter before the aligned trio was replaced, and so indistinguishable
    // from "allocates nothing", the claim the probe exists to check. Capacity
    // escapes through `g_sink` so the object cannot be elided.
    //
    // The floor is the object PLUS its storage. A RingBuffer allocates TWICE:
    // the object through the aligned new, and `storage_` through the ordinary
    // unaligned one the counter could already see. So a floor of the object
    // alone proves NOTHING -- 192 bytes buried under 4096 of storage; under
    // the mutation that removes the aligned trio that form still read 4135 and
    // stayed GREEN. The storage term is what makes the remaining 192 the
    // margin, and `bit_ceil` only rounds capacity UP, so it is a floor for any
    // count, not just a power of two.
    const std::size_t count = g_opaqueCount;
    const std::size_t floorBytes = sizeof(rta::dsp::RingBuffer<float>) + count * sizeof(float);
    std::size_t counted = 0;
    {
        const rta::test::AllocationProbe probe;
        auto rb = std::make_unique<rta::dsp::RingBuffer<float>>(count);
        g_sink = static_cast<double>(rb->capacity());
        counted = probe.bytes();
    }
    INFO("make_unique<RingBuffer<float>>(" << count << ") counted " << counted << " bytes; floor "
                                           << floorBytes << " = sizeof "
                                           << sizeof(rta::dsp::RingBuffer<float>) << " + storage "
                                           << count * sizeof(float));
    CHECK(counted >= floorBytes);
}
