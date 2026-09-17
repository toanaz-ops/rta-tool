// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L-API Task C (record docs/dsp/2026-09-16-remote-api.md sec.3, sec.8,
// sec.9; sec.11 item 7). C1-C8 only -- Task B's ten cases are in
// test_api_policy.cpp, and eighteen in one file overruns the 400-line cap.
//
// Both subjects here are REAL-TIME-SAFETY controls, not hygiene: an uncapped
// poll rate and an uncapped ?points= are the two ways a remote caller can
// make this program do unbounded work while a show is running.
//
// The limiter takes an INJECTED time_point. No real time, no sleeps: a
// timing test that sleeps is a test that will be flaky on a CI runner, and
// after record sec.15 R15 this file runs on ubuntu and macos as well.

#include <catch2/catch_test_macros.hpp>

#include "api/ApiPolicy.h"
#include "api/ApiSettings.h"

#include <chrono>
#include <optional>
#include <string>

using rta::api::ApiSettings;
using rta::api::conditionalVerdict;
using rta::api::etagFor;
using rta::api::RateLimiter;
using rta::api::Verdict;

namespace {

using Clock = std::chrono::steady_clock;
constexpr Clock::time_point kT0{};

[[nodiscard]] constexpr Clock::time_point at(long long ms) {
    return kT0 + std::chrono::milliseconds{ms};
}

/// Admits as many as it can up to `attempts` at one instant and returns how
/// many were admitted. Used where the COUNT is the assertion (C4), so the
/// test states a number rather than a sequence of booleans.
[[nodiscard]] int admitted(RateLimiter& limiter, Clock::time_point now, int attempts) {
    int count = 0;
    for (int i = 0; i < attempts; ++i) {
        if (limiter.admit(now)) {
            ++count;
        }
    }
    return count;
}

}  // namespace

TEST_CASE("C1 the ceiling is exactly the shipped default", "[api][limits]") {
    // 30 is READ from ApiSettings{}, never written as a literal here: a test
    // that hard-codes the number stops testing the setting the day the
    // setting changes (memory/a-default-must-be-run-through-the-gate-it-feeds.md).
    const ApiSettings settings{};
    RateLimiter limiter{settings.maxRequestsPerSecond};
    CHECK(admitted(limiter, kT0, settings.maxRequestsPerSecond)
          == settings.maxRequestsPerSecond);
    CHECK_FALSE(limiter.admit(kT0));   // the 31st
}

TEST_CASE("C2 the window advances", "[api][limits]") {
    const ApiSettings settings{};
    RateLimiter limiter{settings.maxRequestsPerSecond};
    REQUIRE(admitted(limiter, kT0, settings.maxRequestsPerSecond)
            == settings.maxRequestsPerSecond);
    CHECK_FALSE(limiter.admit(kT0));
    CHECK_FALSE(limiter.admit(at(500)));   // half a window: still refused
    CHECK(limiter.admit(at(1500)));
}

TEST_CASE("C3 the boundary is >=, and t0+1000ms exactly is the value that says so",
          "[api][limits]") {
    // The only value that separates `elapsed >= window` from
    // `elapsed > window`. 999 and 1001 pass under either comparison, which is
    // why testing only those two left the shipped semantics unstated.
    const ApiSettings settings{};
    RateLimiter limiter{settings.maxRequestsPerSecond};
    REQUIRE(admitted(limiter, kT0, settings.maxRequestsPerSecond)
            == settings.maxRequestsPerSecond);
    CHECK_FALSE(limiter.admit(at(999)));
    CHECK(limiter.admit(at(1000)));
    CHECK(limiter.admit(at(1001)));
}

TEST_CASE("C4 the window slides, and a fixed window is what this refutes", "[api][limits]") {
    // A fixed window admits `max` at the end of one window and `max` at the
    // start of the next -- sixty loads inside one real second -- while
    // record sec.4's accounting is that 30 is a HARD BOUND on this thread's
    // traffic against the publish slot. A bound that holds only on aligned
    // seconds is not that bound.
    const ApiSettings settings{};
    const int half = settings.maxRequestsPerSecond / 2;
    RateLimiter limiter{settings.maxRequestsPerSecond};

    CHECK(admitted(limiter, kT0, half) == half);
    CHECK(admitted(limiter, at(900), settings.maxRequestsPerSecond - half)
          == settings.maxRequestsPerSecond - half);
    CHECK_FALSE(limiter.admit(at(950)));   // 30 in flight across [t0, t0+950]

    // At t0+1001 the first `half` have aged out and nothing else has. A
    // FIXED window would reset the whole counter here and admit 30 more,
    // for a real-second total of 45. A sliding one admits exactly `half`.
    CHECK(admitted(limiter, at(1001), settings.maxRequestsPerSecond) == half);
}

TEST_CASE("C5 the ETag is the sequence, quoted", "[api][limits]") {
    // The quotes are part of an HTTP entity tag (RFC 9110 sec.8.8.3); a bare
    // 12345 is a malformed validator and a conforming client will not echo
    // it, so every conditional GET silently degrades to a full response.
    CHECK(etagFor(12345) == "\"12345\"");
    CHECK(etagFor(0) == "\"0\"");
    CHECK(etagFor(18446744073709551615ull) == "\"18446744073709551615\"");
}

TEST_CASE("C6 a matching validator is 304", "[api][limits]") {
    CHECK(conditionalVerdict("\"12345\"", std::nullopt, 12345) == Verdict::NotModified);
    CHECK(conditionalVerdict("\"12344\"", std::nullopt, 12345) == Verdict::Serve);
    CHECK(conditionalVerdict("", std::nullopt, 12345) == Verdict::Serve);
    // If-None-Match: * matches any current representation (RFC 9110 sec.13.1.2).
    CHECK(conditionalVerdict("*", std::nullopt, 12345) == Verdict::NotModified);
    // A bare, unquoted validator is malformed and must not match.
    CHECK(conditionalVerdict("12345", std::nullopt, 12345) == Verdict::Serve);
}

TEST_CASE("C7 ?since= needs no server-issued validator", "[api][limits]") {
    CHECK(conditionalVerdict("", 12345, 12345) == Verdict::NotModified);
    CHECK(conditionalVerdict("", 12344, 12345) == Verdict::Serve);
    // A client AHEAD of the server is a client that restarted, or a server
    // that did. Serving it is the only recovery; refusing would strand it.
    CHECK(conditionalVerdict("", 12346, 12345) == Verdict::Serve);
}

TEST_CASE("C8 with both present and disagreeing, If-None-Match wins", "[api][limits]") {
    // The record specifies the header as the primary form and ?since= as the
    // form that needs no server-issued validator. A client that sends both
    // has a validator, so the validator decides.
    CHECK(conditionalVerdict("\"12345\"", 12344, 12345) == Verdict::NotModified);
    CHECK(conditionalVerdict("\"12344\"", 12345, 12345) == Verdict::Serve);
}
