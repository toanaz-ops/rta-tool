// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L-API Task I, station-5 verify pass on PR #18. Every way OUT of the
// request path, and -- the part PR #18 got wrong -- IN WHAT ORDER, plus the
// method surface `Allow` actually advertises.
//
// Same seam the repo already uses for test_alignment_wizard_refusals.cpp:
// the happy path and the wire format live in test_api_server.cpp, and the
// refusals grew into their own subject once "which refusal, and does it cost
// the legitimate client anything" turned out to be a different question from
// "does the refusal happen at all".

#include <catch2/catch_test_macros.hpp>

#include "ApiFixture.h"
#include "RawHttpClient.h"

#include "api/ApiRoutes.h"
#include "api/ApiServer.h"
#include "api/ApiSettings.h"

#include "measure/SnapshotSource.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using rta::api::ApiServer;
using rta::api::ApiSettings;
using rta::api::test::hasHeader;
using rta::api::test::headerValue;
using rta::api::test::makeApiFixture;
using rta::api::test::sendRaw;

namespace {

class Fixture {
public:
    explicit Fixture(ApiSettings settings)
        : source_(std::make_shared<const rta::measure::Snapshot>(makeApiFixture())),
          server_(std::make_unique<ApiServer>(source_, std::move(settings))) {}

    [[nodiscard]] int port() const { return server_->boundPort(); }

private:
    rta::measure::StaticSnapshotSource source_;
    std::unique_ptr<ApiServer> server_;
};

[[nodiscard]] ApiSettings ephemeral() {
    ApiSettings settings;
    settings.enabled = true;
    settings.port = 0;
    return settings;
}

/// One request, with the `Host` field spelled by the caller so a forged or a
/// duplicated one is as easy to send as a correct one.
[[nodiscard]] std::string wire(std::string_view method, std::string_view target,
                               std::string_view hostLines) {
    std::string out = std::string(method) + ' ' + std::string(target) + " HTTP/1.1\r\n";
    out += hostLines;
    out += "Connection: close\r\n\r\n";
    return out;
}

[[nodiscard]] std::string goodHost(int port) {
    return "Host: 127.0.0.1:" + std::to_string(port) + "\r\n";
}

[[nodiscard]] std::string forgedHost(int port) {
    return "Host: attacker.example:" + std::to_string(port) + "\r\n";
}

}  // namespace

// --- the defect the verifier measured -------------------------------------

TEST_CASE("a forged Host does not spend the legitimate client's rate-limit quota") {
    // PR #18 as first written ran the limiter FIRST, so every refused request
    // was admitted into the sliding window before anything decided to refuse
    // it. At a limit of 3, three forged-Host requests filled the window and
    // the legitimate fourth got 429 -- a remote caller who cannot read one
    // byte of this API could still deny it to the operator, from outside the
    // Host allowlist, with no token.
    //
    // The limiter now runs LAST, immediately before routing. sec.4's
    // accounting is a bound on `SnapshotSource::latest()` LOADS, and every
    // refusal above it returns without touching the publish slot -- so a
    // refused request has no load to bound and must not consume a slot.
    ApiSettings settings = ephemeral();
    settings.maxRequestsPerSecond = 3;
    Fixture fixture(settings);
    const int port = fixture.port();

    for (int i = 0; i < 6; ++i) {
        const auto refused = sendRaw(port, wire("GET", "/api/v1/status", forgedHost(port)));
        CHECK(refused.status == 403);
    }

    const auto served = sendRaw(port, wire("GET", "/api/v1/status", goodHost(port)));
    CHECK(served.status == 200);
}

TEST_CASE("a refused method and a refused token do not spend the quota either") {
    // The same argument, one level down: 405 and 401 also return before any
    // load. Three separate refusal kinds, one property.
    ApiSettings settings = ephemeral();
    settings.maxRequestsPerSecond = 2;
    settings.token = "s3cr3t";
    Fixture fixture(settings);
    const int port = fixture.port();

    for (int i = 0; i < 4; ++i) {
        CHECK(sendRaw(port, wire("POST", "/api/v1/status", goodHost(port))).status == 405);
    }
    for (int i = 0; i < 4; ++i) {
        CHECK(sendRaw(port, wire("GET", "/api/v1/status", goodHost(port))).status == 401);
    }

    std::string authorised = "GET /api/v1/status HTTP/1.1\r\n" + goodHost(port);
    authorised += "Authorization: Bearer s3cr3t\r\nConnection: close\r\n\r\n";
    CHECK(sendRaw(port, authorised).status == 200);
}

TEST_CASE("the limiter still bounds what it exists to bound -- SERVED requests") {
    // Moving the limiter last must not have turned it off. Its subject is
    // the number of loads per second, so the bound is measured on requests
    // that actually reach one.
    ApiSettings settings = ephemeral();
    settings.maxRequestsPerSecond = 2;
    Fixture fixture(settings);
    const int port = fixture.port();

    CHECK(sendRaw(port, wire("GET", "/api/v1/status", goodHost(port))).status == 200);
    CHECK(sendRaw(port, wire("GET", "/api/v1/status", goodHost(port))).status == 200);
    CHECK(sendRaw(port, wire("GET", "/api/v1/status", goodHost(port))).status == 429);
}

// --- more than one Host field ---------------------------------------------

TEST_CASE("two Host fields are 400, so a good one cannot carry a forged one") {
    // RFC 9112 sec.3.2: a server MUST answer 400 to a request carrying more
    // than one Host field line. Without that, `get_header_value("Host")`
    // reads only the FIRST -- so `Host: 127.0.0.1:<port>` followed by
    // `Host: attacker.example:<port>` passes the allowlist while every proxy,
    // cache and log downstream may read the other one. Checking the count is
    // one call and closes it.
    Fixture fixture(ephemeral());
    const int port = fixture.port();

    const auto goodThenForged =
        sendRaw(port, wire("GET", "/api/v1/status", goodHost(port) + forgedHost(port)));
    CHECK(goodThenForged.status == 400);

    const auto forgedThenGood =
        sendRaw(port, wire("GET", "/api/v1/status", forgedHost(port) + goodHost(port)));
    CHECK(forgedThenGood.status == 400);

    // And two IDENTICAL Host fields are refused too, deliberately: the rule
    // is one field line, not one distinct value. "Reject when they differ"
    // would leave the parser-disagreement class open for the cost of the
    // same comparison.
    const auto twice = sendRaw(port, wire("GET", "/api/v1/status", goodHost(port) + goodHost(port)));
    CHECK(twice.status == 400);
}

// --- the method surface `Allow` advertises --------------------------------

TEST_CASE("OPTIONS answers on every route, with the Allow it advertises") {
    // PR #18 advertised `Allow: GET, HEAD, OPTIONS` on a 405 and permitted
    // OPTIONS in `methodIsAllowed`, while `installRoutes` registered `Get`
    // only -- so OPTIONS passed the allowlist, found no route, and answered
    // 404 with no `Allow` at all. An API that names a method it does not
    // serve is worse than one that names fewer.
    //
    // Resolved by SERVING it: 204 with `Allow`, no body, and NO snapshot read
    // -- OPTIONS describes the resource, so it must not depend on a
    // measurement existing.
    Fixture fixture(ephemeral());
    const int port = fixture.port();

    for (const char* path : {"status", "snapshot", "transfer", "mtw", "bands", "spectrum",
                             "average", "positions"}) {
        const std::string target = std::string("/api/v1/") + path;
        const auto response = sendRaw(port, wire("OPTIONS", target, goodHost(port)));
        CHECK(response.status == 204);
        CHECK(headerValue(response, "Allow") == "GET, HEAD, OPTIONS");
        CHECK(response.body.empty());
    }

    // An unknown path is still 404 for OPTIONS: the method is served on the
    // eight resources that exist, not as a wildcard.
    CHECK(sendRaw(port, wire("OPTIONS", "/api/v1/nope", goodHost(port))).status == 404);
}

// --- the endpoint table itself, with no server in the picture -------------

TEST_CASE("the route table names eight distinct endpoints and every one serialises") {
    // ApiRoutes.h is framework-free and server-library-free, so this runs
    // against the table directly rather than through eight sockets. It is the
    // regression lock for sec.15 R12's count: EIGHT endpoints, which is not
    // the six names in /status's `available`.
    const auto routes = rta::api::apiRoutes();
    REQUIRE(routes.size() == 8);

    std::vector<std::string> paths;
    for (const auto& entry : routes) {
        REQUIRE(entry.path != nullptr);
        REQUIRE(entry.body != nullptr);
        paths.emplace_back(entry.path);
    }
    const std::vector<std::string> expected{"status", "snapshot", "transfer", "mtw",
                                            "bands",  "spectrum", "average",  "positions"};
    CHECK(paths == expected);

    // Distinct, because two entries sharing a path would register two
    // handlers on one pattern and httplib serves the FIRST -- a silent
    // shadowing nothing else in this suite would notice.
    std::vector<std::string> sorted = paths;
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::unique(sorted.begin(), sorted.end()) == sorted.end());

    // And every adapter really reaches a serialiser: a body that returned ""
    // would satisfy a status-code assertion over the wire and fail here.
    const auto snapshot = makeApiFixture();
    const rta::api::Request shape;
    const ApiSettings settings;
    for (const auto& entry : routes) {
        const std::string body = entry.body(snapshot, shape, settings);
        CHECK(body.size() > 2);
        CHECK(body.front() == '{');
        CHECK(body.back() == '}');
    }
}

TEST_CASE("HEAD answers on every route with headers and no body") {
    // HEAD needs no registration and this case is the evidence: httplib
    // dispatches GET and HEAD to the same `get_handlers_` and suppresses the
    // body on the way out, so the eight `Get` routes serve it. The `ETag`
    // must survive, because a client polling with HEAD + If-None-Match is
    // the cheapest form of sec.3's contract.
    Fixture fixture(ephemeral());
    const int port = fixture.port();

    for (const char* path : {"status", "snapshot", "transfer", "mtw", "bands", "spectrum",
                             "average", "positions"}) {
        const std::string target = std::string("/api/v1/") + path;
        const auto response = sendRaw(port, wire("HEAD", target, goodHost(port)));
        CHECK(response.status == 200);
        CHECK(response.body.empty());
        CHECK(hasHeader(response, "ETag"));
    }
}
