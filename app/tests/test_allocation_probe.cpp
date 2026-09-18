// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The PROBE's own tests (L6a task W0-B0 cases B0b and B0c), split out of
// test_average_group.cpp along the seam that made that file long: "does the
// counting allocator work" is a different subject from "is the spatial
// average's publish churn O(1) in N", and the first one is what every later
// measuring file (test_spl_meter.cpp, test_spl_session.cpp) depends on.
//
// What the two cases here are for:
//   B0b  the replaced global operator new is ONE DEFINITION PER PROGRAM, and
//        the linker succeeding is only half that proof.
//   B0c  the probe RESETS on construction, so one TEST_CASE cannot read
//        another's bytes -- Catch2 runs every file in this binary in one
//        process, and the counter is program-global.
#include <catch2/catch_test_macros.hpp>

#include "AllocationProbe.h"
#include "CodeLines.h"

#include <cstddef>
#include <filesystem>
#include <new>
#include <string>
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
    // No Catch2 macro runs inside the armed scope: an assertion's own
    // bookkeeping is an allocation this counter would charge to the
    // measurement, which is why `counted` is read before anything is checked.
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
    REQUIRE(count >= 16);

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
    // construction, so `first` cannot leak into it. Catch2 runs every file in
    // this binary in one process, so this is not hypothetical.
    std::size_t second = 0;
    {
        const rta::test::AllocationProbe probe;
        std::vector<double> b(count / 64);
        b[0] = 1.0;
        g_sink = b.front() + b.back();
        second = probe.bytes();
    }
    INFO("first = " << first << ", second = " << second);
    CHECK(second >= (count / 64) * sizeof(double));
    CHECK(second < first);

    // Counting is OFF once the guard leaves scope: the reading does not move.
    {
        std::vector<double> after(count * 4, 2.0);
        g_sink = after.back();
        CHECK(after.size() == count * 4);
    }
    CHECK(rta::test::allocationBytes() == second);
}
