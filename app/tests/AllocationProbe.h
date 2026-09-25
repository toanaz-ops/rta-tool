// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The ONE counting allocator for the whole `rtatool_analysis_tests` binary
// (L6a task W0-B0).
//
// A replaceable global `operator new` / `operator delete` is ONE DEFINITION
// PER PROGRAM. Every test file in this target links into one executable, so a
// file that copies the pair is a duplicate-symbol link error, and a file that
// declares its own counters instead cannot see the bytes another translation
// unit's allocations went through. The definitions therefore live in
// AllocationProbe.cpp and nowhere else; test_allocation_probe.cpp's own B0b
// case scans this directory and fails if a second one appears.
//
// ONE CAVEAT A CALLER MUST KNOW, measured on macos-latest in run 35306075307:
// libc++ reaches the heap through `__builtin_operator_new`, which clang is
// permitted to ELIDE for a matched new/delete pair whose pointer never
// escapes. So a reading of zero means "nothing was allocated", which is not
// the same statement as "the path allocates nothing" if the buffer under
// measurement is local and dead. Measure something whose result escapes --
// every shipped caller does, since each one asserts on a published value.
//
// WHAT IS REPLACED, and therefore what the counter can see. Exactly six
// functions: the unaligned trio `operator new(size_t)`,
// `operator delete(void*)`, `operator delete(void*, size_t)`, and the C++17
// over-aligned trio `operator new(size_t, align_val_t)`,
// `operator delete(void*, align_val_t)`,
// `operator delete(void*, size_t, align_val_t)`.
//
// The array and nothrow forms need no replacement, because the standard's own
// defaults FORWARD into the six above: `operator new[]` returns
// `operator new`, the nothrow news call the throwing news and catch, and
// `operator delete[]` and the nothrow deletes call the scalar deletes. So a
// `new T[n]` and every `std::vector` are counted, at either alignment.
//
// With the aligned trio replaced, an OVER-ALIGNED allocation is counted too --
// a type whose alignment exceeds `__STDCPP_DEFAULT_NEW_ALIGNMENT__` (16 here)
// routes to `operator new(size_t, align_val_t)`, and `rta::dsp::RingBuffer`
// (`alignas(64)` members) is exactly such a type. test_allocation_probe.cpp's
// B0d cases prove it: a direct aligned `::operator new`, a `std::vector` of an
// over-aligned element, and a heap-allocated `RingBuffer<float>`.
//
// ONE THING IS STILL INVISIBLE, and a reading of zero does not distinguish it
// from "nothing allocated": an allocation the optimiser removed; see the
// caveat above.
//
// This header declares no allocation function of its own -- keep it that way.
#pragma once

#include <cstddef>

namespace rta::test {

/// Zeroes the byte counter. Called for you by `AllocationProbe`'s constructor.
void resetAllocationProbe() noexcept;

/// Bytes counted while the probe was armed. Readable after the guard has left
/// scope, which is what lets a helper return a measurement it took inside one.
[[nodiscard]] std::size_t allocationBytes() noexcept;

/// Arms or disarms counting. Prefer the scope guard below; this exists for the
/// rare case where the armed window cannot be a lexical scope.
void setAllocationCounting(bool active) noexcept;

/// Arms the counter for a scope, resetting it first.
///
/// The reset is the point: the counter is PROGRAM-global and Catch2 runs every
/// file in this binary in one process, so a measurement that did not reset
/// would read whatever an earlier `TEST_CASE` allocated. Counting stops on
/// destruction, so nothing outside the scope is charged to it either.
///
/// PER-THREAD ATTRIBUTION (round 4, PR #31): only allocations on the thread
/// that CONSTRUCTED this guard are counted, even though `g_countingActive`
/// itself is one process-wide flag every thread's `operator new` checks. Fix
/// for a flaky ubuntu-latest CI failure: `SplLogPipeline`'s writer thread
/// (SplLogPipeline.cpp) does its own legitimate allocating (opening a file,
/// building a log row's `std::string`) concurrently with whatever the main
/// test thread is measuring, and a probe with no thread attribution charged
/// those to the wrong scope. Every EXISTING caller of this class already
/// arms the probe and runs the measured code on the SAME thread (its own),
/// so this is the same behaviour for them, never a narrower one -- it only
/// EXCLUDES allocations this class previously (incorrectly) counted from
/// somewhere else.
class AllocationProbe {
public:
    AllocationProbe() noexcept;
    ~AllocationProbe() noexcept;

    AllocationProbe(const AllocationProbe&) = delete;
    AllocationProbe& operator=(const AllocationProbe&) = delete;
    AllocationProbe(AllocationProbe&&) = delete;
    AllocationProbe& operator=(AllocationProbe&&) = delete;

    /// Bytes counted so far in this scope.
    [[nodiscard]] std::size_t bytes() const noexcept;
};

}  // namespace rta::test
