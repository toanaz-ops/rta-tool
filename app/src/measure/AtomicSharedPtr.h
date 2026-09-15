// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
#pragma once

#include <atomic>
#include <memory>
#include <utility>

/// RTA_ATOMIC_SHARED_PTR_IS_STD -- 1 when this toolchain ships the C++20
/// `std::atomic<std::shared_ptr<T>>` partial specialisation (P0718), 0 when
/// `AtomicSharedPtr` below has to fall back to the `std::atomic_*` free
/// functions. `__cpp_lib_atomic_shared_ptr` is the standard feature-test
/// macro for it, so a library that ships the specialisation later switches
/// over with no edit here.
///
/// RTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK forces the second branch on a
/// toolchain that has the specialisation. It exists because the fallback is
/// the branch that only Apple libc++ compiles, which means it is the branch
/// nobody on this project can break and notice: define the macro, build, and
/// run the [atomicsharedptr] tests to exercise it on a machine that is not a
/// Mac. Never define it in a shipping build -- it opts into functions the
/// standard removes in C++26 for no gain.
#if defined(RTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK)
#define RTA_ATOMIC_SHARED_PTR_IS_STD 0
#elif defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
#define RTA_ATOMIC_SHARED_PTR_IS_STD 1
#else
#define RTA_ATOMIC_SHARED_PTR_IS_STD 0
#endif

/// The fallback path calls functions the standard deprecated in C++20 ON
/// PURPOSE (see AtomicSharedPtr's comment). Silence that ONE diagnostic
/// around those calls and nowhere else, so a deprecation warning anywhere
/// else in this translation unit is still heard.
#if defined(__clang__) || defined(__GNUC__)
#define RTA_ASP_SILENCE_DEPRECATED_PUSH                                      \
    _Pragma("GCC diagnostic push")                                           \
        _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#define RTA_ASP_SILENCE_DEPRECATED_POP _Pragma("GCC diagnostic pop")
#elif defined(_MSC_VER)
// MSVC has the specialisation and normally never reaches the fallback, but
// RTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK exists precisely so it can -- and
// there the deprecation arrives as C4996, which /W4 does report.
#define RTA_ASP_SILENCE_DEPRECATED_PUSH                                      \
    __pragma(warning(push)) __pragma(warning(disable : 4996))
#define RTA_ASP_SILENCE_DEPRECATED_POP __pragma(warning(pop))
#else
#define RTA_ASP_SILENCE_DEPRECATED_PUSH
#define RTA_ASP_SILENCE_DEPRECATED_POP
#endif

namespace rta::measure {

/// An atomically swappable `std::shared_ptr<T>` -- the publish primitive the
/// real-time contract in CLAUDE.md is written in ("publishes immutable
/// snapshots by atomic pointer swap"). Writer is the analysis thread, reader
/// is the message thread; the audio callback never touches either side.
///
/// WHY THIS TYPE EXISTS, AND WHY `std::atomic<std::shared_ptr<T>>` IS NOT
/// WRITTEN DIRECTLY ANY MORE. That partial specialisation is C++20, and
/// Apple's libc++ has not shipped it. There, `std::atomic<T>` resolves to the
/// PRIMARY template, which static_asserts that T is trivially copyable, and
/// the build dies inside <__atomic/atomic.h>:
///
///   error: static assertion failed due to requirement
///   'is_trivially_copyable<std::shared_ptr<const rta::measure::Snapshot>>::value':
///   std::atomic<T> requires that 'T' be a trivially copyable type
///
/// That is how CI's macos-latest job failed to COMPILE on 2026-09-06 (run
/// 34043346767) while MSVC and libstdc++ built the same line without comment.
/// It is a missing library feature, not a mistake in the design, so the fix
/// is to ask for the feature and fall back when the answer is no -- never to
/// reach for a mutex, which would put a lock where the contract says a swap
/// goes.
///
/// The fallback is the `std::atomic_*` overloads for `shared_ptr` (C++11,
/// deprecated in C++20 precisely BECAUSE the atomic<> specialisation replaced
/// them, removed in C++26). They are the same operations with the same
/// memory-order semantics, and libc++ implements them today. The deprecation
/// warning is suppressed only around the three calls that need it, so a real
/// deprecation elsewhere is still heard.
///
/// LOCK-FREEDOM, HONESTLY. Neither path is lock-free on any toolchain this
/// project builds on: MSVC's specialisation spins on the control block, and
/// libc++'s free functions take a lock from a small address-keyed spinlock
/// pool. The fallback is therefore not a downgrade -- it is what the platform
/// was already giving us. It is also why the audio callback must keep its
/// hands off this type: this is the ANALYSIS thread's publish primitive, and
/// the callback's only job stays copying into the lock-free ring buffer.
///
/// Deliberately neither copyable nor movable, exactly like `std::atomic`: a
/// publish slot is a fixed location two threads agreed on, and moving one out
/// from under a reader is never what the caller meant.
template <typename T>
class AtomicSharedPtr {
public:
    using Ptr = std::shared_ptr<T>;

    AtomicSharedPtr() = default;
    explicit AtomicSharedPtr(Ptr initial) : value_(std::move(initial)) {}

    AtomicSharedPtr(const AtomicSharedPtr&) = delete;
    AtomicSharedPtr& operator=(const AtomicSharedPtr&) = delete;
    AtomicSharedPtr(AtomicSharedPtr&&) = delete;
    AtomicSharedPtr& operator=(AtomicSharedPtr&&) = delete;
    ~AtomicSharedPtr() = default;

    [[nodiscard]] Ptr load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
#if RTA_ATOMIC_SHARED_PTR_IS_STD
        return value_.load(order);
#else
        RTA_ASP_SILENCE_DEPRECATED_PUSH
        return std::atomic_load_explicit(&value_, order);
        RTA_ASP_SILENCE_DEPRECATED_POP
#endif
    }

    void store(Ptr desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
#if RTA_ATOMIC_SHARED_PTR_IS_STD
        value_.store(std::move(desired), order);
#else
        RTA_ASP_SILENCE_DEPRECATED_PUSH
        std::atomic_store_explicit(&value_, std::move(desired), order);
        RTA_ASP_SILENCE_DEPRECATED_POP
#endif
    }

    /// Read-and-replace in one step. Not used by the publish path (which only
    /// ever stores), but it is what a consumer that must take ownership of a
    /// one-shot result exactly once would need, and leaving it out would
    /// invite the next caller to reach around this type for it.
    [[nodiscard]] Ptr exchange(Ptr desired,
                               std::memory_order order = std::memory_order_seq_cst) noexcept {
#if RTA_ATOMIC_SHARED_PTR_IS_STD
        return value_.exchange(std::move(desired), order);
#else
        RTA_ASP_SILENCE_DEPRECATED_PUSH
        return std::atomic_exchange_explicit(&value_, std::move(desired), order);
        RTA_ASP_SILENCE_DEPRECATED_POP
#endif
    }

    /// True when this build got the C++20 specialisation rather than the
    /// deprecated free functions. Reported by test_atomic_shared_ptr.cpp so
    /// which path a given runner took is on the record rather than guessed at.
    [[nodiscard]] static constexpr bool usesStdAtomicSpecialisation() noexcept {
        return RTA_ATOMIC_SHARED_PTR_IS_STD != 0;
    }

private:
#if RTA_ATOMIC_SHARED_PTR_IS_STD
    std::atomic<Ptr> value_;
#else
    // The free-function overloads used above take `const shared_ptr<T>*` for
    // the load, so no `mutable` is needed to keep load() const.
    Ptr value_;
#endif
};

}  // namespace rta::measure
