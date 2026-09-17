// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L-API Task B (docs/plans/2026-09-17-remote-api-impl-plan.md; record
// docs/dsp/2026-09-16-remote-api.md sec.8, sec.9, sec.11 items 6, 8, 9).
//
// Every case here runs against `ApiSettings{}` -- the SHIPPED defaults
// (memory/a-default-must-be-run-through-the-gate-it-feeds.md) -- EXCEPT
// B7-B9, and that exception is the finding. The shipped token is empty, so
// at the defaults the Bearer control never executes; a suite that only ever
// used the defaults would ship an untested authentication path that LOOKS
// tested (memory/a-fixed-defect-returns-through-the-silent-fallback.md).
//
// B1-B10 only. Task C's eight cases open their own file: at this suite's
// measured density eighteen cases in one file overruns CLAUDE.md's 400-line
// cap, and discovering a cap by breaking it is how a file ends up at 399
// lines doing two jobs.

#include <catch2/catch_test_macros.hpp>

#include "api/ApiPolicy.h"
#include "api/ApiSettings.h"

#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

using rta::api::ApiSettings;
using rta::api::bearerAccepted;
using rta::api::bodyIsAcceptable;
using rta::api::clampPoints;
using rta::api::hostIsAllowed;
using rta::api::Method;
using rta::api::methodIsAllowed;
using rta::api::methodOf;
using rta::api::startRefusal;

TEST_CASE("B1 the Host allowlist is the whole DNS-rebinding defence", "[api][policy]") {
    const ApiSettings settings{};
    CHECK(hostIsAllowed("127.0.0.1:4736", settings.port));
    CHECK(hostIsAllowed("localhost:4736", settings.port));
    CHECK(hostIsAllowed("[::1]:4736", settings.port));
    CHECK_FALSE(hostIsAllowed("attacker.example:4736", settings.port));
    // The substring trap NCC Group's "strictly contain" wording exists to
    // catch: a naive find() passes this, and the attacker owns the name.
    CHECK_FALSE(hostIsAllowed("127.0.0.1:4736.attacker.example", settings.port));
    CHECK_FALSE(hostIsAllowed("", settings.port));
    CHECK_FALSE(hostIsAllowed("127.0.0.1", settings.port));          // no port at all
    CHECK_FALSE(hostIsAllowed("127.0.0.1:4737", settings.port));     // right host, wrong port
}

TEST_CASE("B2 case and whitespace do not open the Host allowlist", "[api][policy]") {
    // A hostname is case-insensitive (RFC 4343), and accepting `localhost`
    // costs nothing against rebinding: a rebound request carries
    // `Host: attacker.example`, because the Host header is the REQUESTED
    // name and never the resolved address. Do not "fix" this to reject it.
    CHECK(hostIsAllowed("LOCALHOST:4736", 4736));
    CHECK(hostIsAllowed("LocalHost:4736", 4736));
    CHECK_FALSE(hostIsAllowed(" 127.0.0.1:4736", 4736));
    CHECK_FALSE(hostIsAllowed("127.0.0.1:4736 ", 4736));
}

TEST_CASE("B3 the method allowlist is case-sensitive", "[api][policy]") {
    CHECK(methodIsAllowed(methodOf("GET")));
    CHECK(methodIsAllowed(methodOf("HEAD")));
    CHECK(methodIsAllowed(methodOf("OPTIONS")));
    CHECK_FALSE(methodIsAllowed(methodOf("POST")));
    CHECK_FALSE(methodIsAllowed(methodOf("PUT")));
    CHECK_FALSE(methodIsAllowed(methodOf("DELETE")));
    CHECK_FALSE(methodIsAllowed(methodOf("PATCH")));
    CHECK_FALSE(methodIsAllowed(methodOf("TRACE")));
    CHECK_FALSE(methodIsAllowed(methodOf("")));
    // The HTTP method token is case-SENSITIVE (RFC 9110 sec.9.1).
    CHECK_FALSE(methodIsAllowed(methodOf("get")));
    CHECK(methodOf("GET") == Method::Get);
    CHECK(methodOf("get") == Method::Other);
}

TEST_CASE("B4 the point cap is run at the shipped default", "[api][policy]") {
    const ApiSettings settings{};
    CHECK(clampPoints(1'000'000, settings) == settings.maxPointsPerResponse);
    // Absent or garbage means "as much as allowed", not zero: a client that
    // omits ?points= wants the whole curve, and a zero-length body would be
    // a silent truncation nobody asked for.
    CHECK(clampPoints(-1, settings) == settings.maxPointsPerResponse);
    CHECK(clampPoints(0, settings) == settings.maxPointsPerResponse);
    CHECK(clampPoints(256, settings) == 256);
    CHECK(clampPoints(settings.maxPointsPerResponse, settings) == settings.maxPointsPerResponse);
    // The parameter is long long precisely so an over-int value clamps
    // rather than wrapping -- signed overflow is UB, and a wrap here would
    // turn a hostile ?points= into a negative count.
    CHECK(clampPoints(static_cast<long long>(std::numeric_limits<int>::max()) + 1, settings)
          == settings.maxPointsPerResponse);
    CHECK(clampPoints(std::numeric_limits<long long>::max(), settings)
          == settings.maxPointsPerResponse);
}

TEST_CASE("B5 allowLanBind refuses rather than silently binding", "[api][policy]") {
    // Record sec.14 q.2's default made visible: the setting EXISTS and the
    // code path does not. Honest about the roadmap, and impossible to turn
    // on by accident.
    ApiSettings lan{};
    lan.allowLanBind = true;
    const auto refusal = startRefusal(lan);
    REQUIRE(refusal.has_value());
    CHECK_FALSE(refusal->empty());
    CHECK_FALSE(startRefusal(ApiSettings{}).has_value());
}

TEST_CASE("B6 a non-loopback bind address is refused the same way", "[api][policy]") {
    // Two ways to ask for a LAN bind, one refusal.
    ApiSettings any{};
    any.bindAddress = "0.0.0.0";
    CHECK(startRefusal(any).has_value());
    ApiSettings lan{};
    lan.bindAddress = "192.168.1.10";
    CHECK(startRefusal(lan).has_value());
    ApiSettings six{};
    six.bindAddress = "::1";
    CHECK_FALSE(startRefusal(six).has_value());
}

TEST_CASE("B7 at the shipped default the token control is OFF, deliberately", "[api][policy]") {
    // Read this before reading B8: out of the box there is NO 401. The
    // shipped default is an empty token, so the Bearer control does not
    // execute, and the defence at the defaults is the Host allowlist plus
    // the loopback bind -- not authentication.
    const ApiSettings settings{};
    CHECK(settings.token.empty());
    CHECK(bearerAccepted("", settings));
    CHECK(bearerAccepted("Bearer anything", settings));
}

TEST_CASE("B8 with a token set, the Bearer control is real", "[api][policy]") {
    // The one deliberate non-default in this file, and the reason is in the
    // name: a control the shipped defaults disable is a control no
    // default-only suite ever runs.
    ApiSettings settings{};
    settings.token = "s3cr3t";
    CHECK(bearerAccepted("Bearer s3cr3t", settings));
    // The scheme token is case-INSENSITIVE (RFC 9110 sec.11.1, inherited by
    // RFC 6750's Bearer); the credential after it is not.
    CHECK(bearerAccepted("bearer s3cr3t", settings));
    CHECK(bearerAccepted("BEARER s3cr3t", settings));
    CHECK_FALSE(bearerAccepted("", settings));
    CHECK_FALSE(bearerAccepted("Bearer wrong", settings));
    CHECK_FALSE(bearerAccepted("Bearer S3CR3T", settings));
    CHECK_FALSE(bearerAccepted("Basic czNjcjN0", settings));
    CHECK_FALSE(bearerAccepted("Bearer  s3cr3t", settings));   // two spaces
    CHECK_FALSE(bearerAccepted("Bearer s3cr3t ", settings));   // trailing space
    CHECK_FALSE(bearerAccepted("s3cr3t", settings));           // no scheme
    CHECK_FALSE(bearerAccepted("token=s3cr3t", settings));     // cookie-shaped
}

TEST_CASE("B9 the forbidden carriers are forbidden by construction", "[api][policy]") {
    // A cookie is AMBIENT AUTHORITY: the browser attaches it to every
    // request to 127.0.0.1:<port> regardless of which page issued it, so any
    // site the operator opens during a show is authenticated to this
    // listener. NOT a rebinding argument -- a cookie jar keys on the host
    // NAME, so a rebound request carries the attacker's cookies, never this
    // app's. A query parameter is worse again: it lands in every log.
    //
    // The assertion is structural: `bearerAccepted` takes the Authorization
    // header and the settings, and nothing else. There is no parameter a
    // cookie or a query string could arrive through.
    ApiSettings settings{};
    settings.token = "s3cr3t";
    CHECK_FALSE(bearerAccepted("Cookie: token=s3cr3t", settings));
    CHECK_FALSE(bearerAccepted("token=s3cr3t", settings));
    CHECK_FALSE(bearerAccepted("?token=s3cr3t", settings));
    static_assert(std::is_same_v<decltype(&bearerAccepted),
                                 bool (*)(std::string_view, const ApiSettings&)>,
                  "bearerAccepted reads the Authorization header and the settings, nothing else");
}

TEST_CASE("B10 the body cap is a number, not a hope", "[api][policy]") {
    // GET requests should carry no body at all. The cap is what makes
    // record sec.9's 415 unreachable rather than unimplemented (sec.15 R17):
    // an oversized body is refused with 413 BEFORE its content type is read.
    const ApiSettings settings{};
    CHECK(bodyIsAcceptable(0, settings));
    CHECK(bodyIsAcceptable(settings.maxRequestBodyBytes, settings));
    CHECK_FALSE(bodyIsAcceptable(settings.maxRequestBodyBytes + 1, settings));
    CHECK_FALSE(bodyIsAcceptable(std::numeric_limits<long long>::max(), settings));
}
