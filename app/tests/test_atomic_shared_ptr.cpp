// SPDX-License-Identifier: AGPL-3.0-or-later
//
// AtomicSharedPtr is the substitute for the C++20 std::atomic specialisation
// over shared_ptr that Apple's libc++ does not ship -- spelled out in
// measure/AtomicSharedPtr.h, and deliberately not spelled literally here,
// because the ctest guard `no_std_atomic_over_shared_ptr` greps for that form
// and lets exactly one file contain it. Two different implementations hide
// behind one interface, and only one is ever compiled on a given runner -- so
// these tests exist to make sure the branch THIS toolchain took is the one
// that gets exercised, rather than trusting that the branch someone happened
// to develop on is the only one that works.
//
// What is NOT claimed here: this is not a proof of linearizability. A unit
// test cannot demonstrate the absence of a race; it can demonstrate that the
// operations have the right values and ownership semantics, and that a
// concurrent writer and reader never hand out a dangling or half-built
// pointer over a few hundred thousand swaps. The memory-order argument lives
// in the header's comment and in the release/acquire pairing at the call
// sites, not in an assertion.

#include <catch2/catch_test_macros.hpp>

#include "measure/AtomicSharedPtr.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using rta::measure::AtomicSharedPtr;

namespace {

struct Payload {
    explicit Payload(int v) : value(v) {}
    int value;
};

}  // namespace

TEST_CASE("An AtomicSharedPtr starts empty", "[atomicsharedptr]") {
    AtomicSharedPtr<const Payload> slot;
    CHECK(slot.load() == nullptr);
}

TEST_CASE("A stored pointer is the pointer that loads back", "[atomicsharedptr]") {
    AtomicSharedPtr<const Payload> slot;
    const auto first = std::make_shared<const Payload>(7);
    slot.store(first, std::memory_order_release);

    const auto read = slot.load(std::memory_order_acquire);
    REQUIRE(read != nullptr);
    CHECK(read.get() == first.get());
    CHECK(read->value == 7);
}

TEST_CASE("A store replaces the previous value and releases its reference",
          "[atomicsharedptr]") {
    AtomicSharedPtr<const Payload> slot;
    std::weak_ptr<const Payload> observer;
    {
        const auto first = std::make_shared<const Payload>(1);
        observer = first;
        slot.store(first, std::memory_order_release);
    }
    // The slot is the only owner now. This is the property that matters for
    // the publish path: a snapshot stays alive exactly as long as the slot or
    // a reader holds it, never one publish longer.
    REQUIRE_FALSE(observer.expired());

    slot.store(std::make_shared<const Payload>(2), std::memory_order_release);
    CHECK(observer.expired());
    CHECK(slot.load(std::memory_order_acquire)->value == 2);
}

TEST_CASE("Storing nullptr clears the slot, as armLocateCapture relies on",
          "[atomicsharedptr]") {
    AtomicSharedPtr<const Payload> slot;
    slot.store(std::make_shared<const Payload>(3), std::memory_order_release);
    REQUIRE(slot.load() != nullptr);

    // AnalysisThread::armLocateCapture() does exactly this to make sure a
    // poller cannot mistake the PREVIOUS Locate's answer for the new one.
    slot.store(nullptr, std::memory_order_relaxed);
    CHECK(slot.load(std::memory_order_acquire) == nullptr);
}

TEST_CASE("An exchange returns the old value and installs the new one",
          "[atomicsharedptr]") {
    const auto first = std::make_shared<const Payload>(10);
    AtomicSharedPtr<const Payload> slot(first);

    const auto previous = slot.exchange(std::make_shared<const Payload>(20));
    REQUIRE(previous != nullptr);
    CHECK(previous.get() == first.get());
    CHECK(slot.load()->value == 20);
}

TEST_CASE("A reader racing a writer never observes a torn or dead pointer",
          "[atomicsharedptr]") {
    // Not a proof of correctness (see this file's header). It is the smoke
    // test that catches the failure the fallback path could plausibly have:
    // a load that copies the shared_ptr without holding whatever the
    // implementation uses to serialise it would hand back a control block
    // already destroyed by the writer, and this loop would crash or read a
    // freed value long before it finished.
    constexpr int kIterations = 200000;
    AtomicSharedPtr<const Payload> slot;
    slot.store(std::make_shared<const Payload>(0), std::memory_order_release);

    std::atomic<bool> go{false};
    std::atomic<int> mismatches{0};
    std::atomic<int> nulls{0};

    std::thread writer([&] {
        while (!go.load(std::memory_order_acquire)) {
        }
        for (int i = 1; i <= kIterations; ++i) {
            slot.store(std::make_shared<const Payload>(i), std::memory_order_release);
        }
    });

    std::thread reader([&] {
        while (!go.load(std::memory_order_acquire)) {
        }
        for (int i = 0; i < kIterations; ++i) {
            const auto seen = slot.load(std::memory_order_acquire);
            if (seen == nullptr) {
                nulls.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            // Dereferencing through the copy the reader now owns is the
            // whole point: it must stay valid even though the writer has
            // already moved on several values.
            if (seen->value < 0 || seen->value > kIterations) {
                mismatches.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    go.store(true, std::memory_order_release);
    writer.join();
    reader.join();

    CHECK(nulls.load() == 0);
    CHECK(mismatches.load() == 0);
    CHECK(slot.load()->value == kIterations);
}

TEST_CASE("Which atomic-shared_ptr implementation this build took is on the record",
          "[atomicsharedptr]") {
    // Always passes; it exists so the ctest log of every runner says which
    // branch of AtomicSharedPtr.h the tests above actually exercised. Without
    // it, "the tests pass" is silent about whether the fallback -- the branch
    // that exists solely for Apple libc++ -- has ever been run at all.
    INFO("AtomicSharedPtr uses "
         << (AtomicSharedPtr<const Payload>::usesStdAtomicSpecialisation()
                 ? "the C++20 std::atomic specialisation"
                 : "the std::atomic_* free-function fallback"));
    CHECK(true);
}
