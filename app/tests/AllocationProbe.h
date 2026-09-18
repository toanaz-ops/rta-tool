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
// AllocationProbe.cpp and nowhere else; test_average_group.cpp's own B0b case
// scans this directory and fails if a second one appears.
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
