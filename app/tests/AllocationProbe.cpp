// SPDX-License-Identifier: AGPL-3.0-or-later
//
// THE ONE AND ONLY definition of the replaced global allocation functions in
// `rtatool_analysis_tests`. Moved here verbatim from test_average_group.cpp
// (L6a task W0-B0) so that test file, test_spl_meter.cpp and every later
// measuring file share one counter instead of failing to link. The probe's
// own cases live in test_allocation_probe.cpp.
//
// SIX functions are replaced: the unaligned trio `operator new(size_t)`,
// `operator delete(void*)`, `operator delete(void*, size_t)`, and the C++17
// over-aligned trio that takes `std::align_val_t`. The aligned set is what
// lets the counter see a type whose alignment exceeds
// `__STDCPP_DEFAULT_NEW_ALIGNMENT__` -- `rta::dsp::RingBuffer` (`alignas(64)`
// members) is one, and before the aligned set was replaced a heap-allocated
// RingBuffer measured zero bytes.
//
// Inert outside an armed window: one relaxed atomic load per allocation.
#include "AllocationProbe.h"

#include <atomic>
#include <cstdlib>
#include <new>
#include <thread>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace {

std::atomic<bool> g_countingActive{false};
std::atomic<std::size_t> g_bytesAllocated{0};
// Round 4 (PR #31): the thread that armed the probe. `g_countingActive` is
// still the one flag every thread's operator new checks -- what changed is
// that a hit ALSO requires this id to match std::this_thread::get_id(), so a
// background thread's own legitimate allocation (SplLogPipeline's writer
// thread opening a file, say) never gets charged to whatever the arming
// thread is measuring. Written (relaxed) strictly BEFORE g_countingActive's
// own release-store in setAllocationCounting(true), and read (relaxed) only
// after operator new's acquire-load of g_countingActive has already
// observed that same release -- the standard release/acquire "publish"
// idiom, so no reader ever sees a torn or stale id.
std::atomic<std::thread::id> g_countingThreadId{};

// The ONLY platform-conditional block in this file, and it is unavoidable.
//
// MSVC: the Microsoft CRT ships neither `std::aligned_alloc` nor
// `posix_memalign`, so `_aligned_malloc` is the only aligned allocator there --
// and a pointer it returns may be released ONLY by `_aligned_free`. Passing one
// to `free` corrupts the heap, which is why `alignedFree` exists at all rather
// than the deletes calling `std::free` directly.
//
// Elsewhere: `posix_memalign`, not C11 `std::aligned_alloc`, because
// `aligned_alloc` requires `size` to be an integral multiple of `alignment`.
// Apple's libc enforces that and returns null when it does not hold, and a
// caller need not satisfy it: `::operator new(100, std::align_val_t{64})` is
// well formed, and so is a vector of an alignas(64) type whose element is
// smaller than 64 bytes with a size the library rounded. POSIX puts no such
// constraint on `size`.
void* alignedAllocate(std::size_t size, std::align_val_t alignment) noexcept {
    const std::size_t requested = static_cast<std::size_t>(alignment);
#if defined(_MSC_VER)
    return _aligned_malloc(size, requested);
#else
    // posix_memalign requires the alignment to be a power of two AND a
    // multiple of sizeof(void*). `std::align_val_t` values below that are
    // perfectly legal C++ (a `new (std::align_val_t{4})` is well formed), so
    // raise the floor rather than hand posix_memalign an argument it rejects.
    // Over-aligning is always safe; under-aligning is not.
    const std::size_t aligned = requested < sizeof(void*) ? sizeof(void*) : requested;
    void* p = nullptr;
    if (posix_memalign(&p, aligned, size) != 0) return nullptr;
    return p;
#endif
}

void alignedFree(void* p) noexcept {
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

}  // namespace

namespace rta::test {

void resetAllocationProbe() noexcept { g_bytesAllocated.store(0, std::memory_order_relaxed); }

std::size_t allocationBytes() noexcept { return g_bytesAllocated.load(std::memory_order_relaxed); }

void setAllocationCounting(bool active) noexcept {
    if (active) {
        // Published BEFORE the release-store below -- see g_countingThreadId's
        // own comment for why this ordering is what makes the reader side
        // (operator new) safe with only a relaxed load of the id itself.
        g_countingThreadId.store(std::this_thread::get_id(), std::memory_order_relaxed);
    }
    g_countingActive.store(active, std::memory_order_release);
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
    // Acquire: pairs with setAllocationCounting(true)'s release-store, so
    // observing `true` here also makes that call's earlier (relaxed) write to
    // g_countingThreadId visible -- see that variable's own comment.
    if (g_countingActive.load(std::memory_order_acquire) &&
        g_countingThreadId.load(std::memory_order_relaxed) == std::this_thread::get_id()) {
        g_bytesAllocated.fetch_add(size, std::memory_order_relaxed);
    }
    return p;
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

// The C++17 over-aligned set. Only these three are replaced, for the same
// reason the unaligned set replaces only three: the standard's own defaults
// FORWARD to them. `operator new[](size, align)` returns
// `operator new(size, align)`; the nothrow forms call the throwing form and
// catch `bad_alloc`; `operator delete[](p, align)` and the nothrow deletes
// call `operator delete(p, align)`. Replacing the throwing scalar new and the
// two scalar deletes therefore already covers `new T[n]`, every `std::vector`
// of an over-aligned element, and `new (std::nothrow) T`.
//
// The delete side MUST release through `alignedFree`, the same allocator
// `alignedAllocate` used. Handing an `_aligned_malloc` pointer to `free` is
// heap corruption on MSVC, not a leak -- and the unaligned trio above stays on
// malloc/free precisely because its pointers came from `std::malloc`. The two
// pairs never cross: the compiler picks the aligned overload from the type's
// alignment at both the new-expression and the delete-expression.
//
// The parameter is unnamed in the deletes because neither release path needs
// it, and /W4 warns about a named parameter nothing reads.
void* operator new(std::size_t size, std::align_val_t alignment) {
    void* p = alignedAllocate(size, alignment);
    if (p == nullptr) throw std::bad_alloc();
    if (g_countingActive.load(std::memory_order_acquire) &&
        g_countingThreadId.load(std::memory_order_relaxed) == std::this_thread::get_id()) {
        g_bytesAllocated.fetch_add(size, std::memory_order_relaxed);
    }
    return p;
}

void operator delete(void* p, std::align_val_t) noexcept { alignedFree(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { alignedFree(p); }
