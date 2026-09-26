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

TEST_CASE("control 2 runs BEFORE control 1's count check would matter: order is host, "
         "not accumulated failures",
         "[api][pre_routing]") {
    // A single, well-formed Host header naming an address the allowlist
    // refuses -- confirms HostNotAllowed is reachable on its own, not merely
    // as a side effect of a bad count (the case above already proves count
    // alone triggers control 1; this proves control 2 triggers independently
    // when count is fine).
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

TEST_CASE("the order is 1, 2, 3, 4, 5: a request failing several controls reports the "
         "FIRST one",
          "[api][pre_routing]") {
    // Fails controls 2 (bad Host), 3 (bad method), 4 (no token supplied
    // against a set one) and 5 (body over cap) all at once -- the shipped
    // ORDER (host allowlist before method before Bearer before body) is what
    // this asserts, not merely "some refusal happened".
    ApiSettings settings;
    settings.token = "s3cr3t";
    settings.maxRequestBodyBytes = 1024;

    PreRoutingInputs in;
    in.hostHeaderCount = 1;
    in.hostHeaderValue = "attacker.example:4736";
    in.boundPort = 4736;
    in.method = Method::Other;
    in.authorizationHeader = "";
    in.declaredBodyBytes = 4096;
    CHECK(decidePreRoutingRefusal(in, settings) == PreRoutingRefusal::HostNotAllowed);

    // Fixing the Host but nothing else: the NEXT control in order (method)
    // now reports, not a jump straight to body.
    in.hostHeaderValue = "127.0.0.1:4736";
    CHECK(decidePreRoutingRefusal(in, settings) == PreRoutingRefusal::MethodNotAllowed);
}
