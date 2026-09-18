// SPDX-License-Identifier: AGPL-3.0-or-later
//
// THE ONE AND ONLY definition of the replaced global allocation functions in
// `rtatool_analysis_tests`. Moved here verbatim from test_average_group.cpp
// (L6a task W0-B0) so that test file, test_spl_meter.cpp and every later
// measuring file share one counter instead of failing to link. The probe's
// own cases live in test_allocation_probe.cpp.
//
// Inert outside an armed window: one relaxed atomic load per allocation.
#include "AllocationProbe.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {

std::atomic<bool> g_countingActive{false};
std::atomic<std::size_t> g_bytesAllocated{0};

}  // namespace

namespace rta::test {

void resetAllocationProbe() noexcept { g_bytesAllocated.store(0, std::memory_order_relaxed); }

std::size_t allocationBytes() noexcept { return g_bytesAllocated.load(std::memory_order_relaxed); }

void setAllocationCounting(bool active) noexcept {
    g_countingActive.store(active, std::memory_order_relaxed);
}

AllocationProbe::AllocationProbe() noexcept {
    resetAllocationProbe();
    setAllocationCounting(true);
}

AllocationProbe::~AllocationProbe() noexcept { setAllocationCounting(false); }

std::size_t AllocationProbe::bytes() const noexcept { return allocationBytes(); }

}  // namespace rta::test

void* operator new(std::size_t size) {
    void* p = std::malloc(size);
    if (p == nullptr) throw std::bad_alloc();
    if (g_countingActive.load(std::memory_order_relaxed)) {
        g_bytesAllocated.fetch_add(size, std::memory_order_relaxed);
    }
    return p;
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
