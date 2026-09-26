// SPDX-License-Identifier: AGPL-3.0-or-later
//
// LOW follow-up batch, item 12: `decidePreRoutingRefusal` (ApiPolicy.h/.cpp)
// is ApiServer.cpp's own pre-routing sequencing (record sec.9 controls 1-5),
// pulled out so it is provable in RTA_BUILD_APP=OFF -- before this it was
// reachable only through a real socket in app/tests_juce/test_api_server*.cpp.
// Each case here fixes every OTHER field at a value that would PASS, so a
// single failing control is what the case's own name says it is, never a
// coincidence of two controls both refusing the same request.
#include <catch2/catch_test_macros.hpp>

#include "api/ApiPolicy.h"
#include "api/ApiSettings.h"

using rta::api::ApiSettings;
using rta::api::decidePreRoutingRefusal;
using rta::api::Method;
using rta::api::PreRoutingInputs;
using rta::api::PreRoutingRefusal;

namespace {

/// Every field set to a value that PASSES every one of the five controls,
/// against `ApiSettings{}` (an empty token, the shipped default body cap) --
/// each TEST_CASE below overrides exactly one field to force exactly one
/// refusal.
PreRoutingInputs passingInputs() {
    PreRoutingInputs in;
    in.hostHeaderCount = 1;
    in.hostHeaderValue = "127.0.0.1:4736";
    in.boundPort = 4736;
    in.method = Method::Get;
    in.authorizationHeader = "";  // settings.token is empty: bearerAccepted(...) == true
    in.declaredBodyBytes = 0;
    return in;
}

}  // namespace

TEST_CASE("decidePreRoutingRefusal passes a request that fails none of the five controls",
          "[api][pre_routing]") {
    CHECK(decidePreRoutingRefusal(passingInputs(), ApiSettings{}) == PreRoutingRefusal::None);
}

TEST_CASE("control 1: more than one Host header refuses before either is read",
          "[api][pre_routing]") {
    auto in = passingInputs();
    in.hostHeaderCount = 2;
    // A VALID hostHeaderValue, deliberately: proves this control refuses on
    // COUNT alone, never on what the (single, as far as this struct can see)
    // value says.
    CHECK(decidePreRoutingRefusal(in, ApiSettings{}) == PreRoutingRefusal::MultipleHostHeaders);
}

TEST_CASE("control 2: the Host allowlist refuses a forged Host, independent of the path",
          "[api][pre_routing]") {
    auto in = passingInputs();
    in.hostHeaderValue = "attacker.example:4736";
    CHECK(decidePreRoutingRefusal(in, ApiSettings{}) == PreRoutingRefusal::HostNotAllowed);
}

TEST_CASE("control 2: the Host allowlist also refuses a right-literal, wrong-port Host",
         "[api][pre_routing]") {
    // Fix-round rename (was "control 2 runs BEFORE control 1's count check
    // would matter" but never set hostHeaderCount past 1, so it never
    // exercised any interaction with control 1 at all -- see the "the order
    // is 1, 2, 3, 4, 5" chain test below for what actually pins that
    // ordering). This case's real, distinct value: a Host header that is
    // well-formed and single (count == 1, passes control 1) but wrong for a
    // DIFFERENT reason than the "control 2" case above (a forged literal) --
    // the right loopback literal with the wrong port -- covering a second
    // branch of hostIsAllowed's own comparison.
    auto in = passingInputs();
    in.hostHeaderCount = 1;
    in.hostHeaderValue = "[::1]:9999";  // right literal, wrong port
    CHECK(decidePreRoutingRefusal(in, ApiSettings{}) == PreRoutingRefusal::HostNotAllowed);
}

TEST_CASE("control 3: an unlisted method refuses, GET/HEAD/OPTIONS still pass",
          "[api][pre_routing]") {
    auto in = passingInputs();
    in.method = Method::Other;
    CHECK(decidePreRoutingRefusal(in, ApiSettings{}) == PreRoutingRefusal::MethodNotAllowed);

    for (const Method allowed : {Method::Get, Method::Head, Method::Options}) {
        auto ok = passingInputs();
        ok.method = allowed;
        CHECK(decidePreRoutingRefusal(ok, ApiSettings{}) == PreRoutingRefusal::None);
    }
}

TEST_CASE("control 4: a set token refuses a missing or wrong Bearer credential",
          "[api][pre_routing]") {
    ApiSettings settings;
    settings.token = "s3cr3t";

    auto missing = passingInputs();
    missing.authorizationHeader = "";
    CHECK(decidePreRoutingRefusal(missing, settings) == PreRoutingRefusal::Unauthorized);

    auto wrong = passingInputs();
    wrong.authorizationHeader = "Bearer nope";
    CHECK(decidePreRoutingRefusal(wrong, settings) == PreRoutingRefusal::Unauthorized);

    auto right = passingInputs();
    right.authorizationHeader = "Bearer s3cr3t";
    CHECK(decidePreRoutingRefusal(right, settings) == PreRoutingRefusal::None);
}

TEST_CASE("control 5: a declared body past the cap refuses before the body is read",
          "[api][pre_routing]") {
    ApiSettings settings;
    settings.maxRequestBodyBytes = 1024;

    auto tooBig = passingInputs();
    tooBig.declaredBodyBytes = 1025;
    CHECK(decidePreRoutingRefusal(tooBig, settings) == PreRoutingRefusal::BodyTooLarge);

    auto atCap = passingInputs();
    atCap.declaredBodyBytes = 1024;
    CHECK(decidePreRoutingRefusal(atCap, settings) == PreRoutingRefusal::None);
}

TEST_CASE("the order is 1, 2, 3, 4, 5: fixing one control at a time reveals the next",
          "[api][pre_routing]") {
    // Fix-round widening: the previous version of this test only fixed the
    // Host and stopped, which pins 2-before-3 but leaves 1<->2 and 4<->5
    // unpinned -- both mutants survived (verifier finding, PR #33 fix
    // round). Starts failing ALL FIVE controls at once and fixes them one at
    // a time, in the shipped order, asserting the NEXT reported refusal at
    // every step. Each assertion is adversarial in the same way: every
    // control AFTER the one just fixed is still failing, so the refusal
    // reported can only be explained by the control immediately checked
    // NEXT actually running next -- a swap of ANY adjacent pair changes
    // which refusal comes back at that step.
    ApiSettings settings;
    settings.token = "s3cr3t";
    settings.maxRequestBodyBytes = 1024;

    PreRoutingInputs in;
    in.hostHeaderCount = 2;                        // fails control 1
    in.hostHeaderValue = "attacker.example:4736";  // fails control 2
    in.boundPort = 4736;
    in.method = Method::Other;         // fails control 3
    in.authorizationHeader = "";       // fails control 4 (token is set)
    in.declaredBodyBytes = 4096;       // fails control 5 (cap is 1024)

    // Step 1: count > 1 AND a forged host both fail -- MultipleHostHeaders
    // only follows if control 1 runs before control 2 (a 1<->2 swap would
    // report HostNotAllowed here instead, since a swapped control 2 would
    // see the forged host first).
    CHECK(decidePreRoutingRefusal(in, settings) == PreRoutingRefusal::MultipleHostHeaders);

    // Step 2: fix the count. Host is still forged, method/token/body all
    // still fail -- HostNotAllowed.
    in.hostHeaderCount = 1;
    CHECK(decidePreRoutingRefusal(in, settings) == PreRoutingRefusal::HostNotAllowed);

    // Step 3: fix the Host. Method/token/body still fail -- MethodNotAllowed.
    in.hostHeaderValue = "127.0.0.1:4736";
    CHECK(decidePreRoutingRefusal(in, settings) == PreRoutingRefusal::MethodNotAllowed);

    // Step 4: fix the method. Token and body both still fail -- Unauthorized
    // only follows if control 4 runs before control 5 (a 4<->5 swap would
    // report BodyTooLarge here instead, since a swapped control 5 would see
    // the oversized body first).
    in.method = Method::Get;
    CHECK(decidePreRoutingRefusal(in, settings) == PreRoutingRefusal::Unauthorized);

    // Step 5: fix the token. Body still over cap -- BodyTooLarge.
    in.authorizationHeader = "Bearer s3cr3t";
    CHECK(decidePreRoutingRefusal(in, settings) == PreRoutingRefusal::BodyTooLarge);

    // Step 6: fix the body. Everything passes.
    in.declaredBodyBytes = 1024;
    CHECK(decidePreRoutingRefusal(in, settings) == PreRoutingRefusal::None);
}
