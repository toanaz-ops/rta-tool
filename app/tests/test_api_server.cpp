// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L-API Task I (record docs/dsp/2026-09-16-remote-api.md sec.4, sec.8,
// sec.9, sec.10, sec.15 R15/R16a). The server over a REAL loopback socket, in
// the RTA_BUILD_APP=OFF target -- the only configuration CI runs, on three
// operating systems. Before R15 the whole network layer, including sec.9's
// "highest-value control in the whole API", was proven on zero CI machines.
//
// NOTHING HERE SLEEPS, and that is a property rather than a style. `ApiServer`
// binds on the CONSTRUCTING thread and only then starts its thread
// (`bind_to_any_port` + `listen_after_bind`), so `boundPort()` is already
// valid when the constructor returns. A test that slept would be asserting
// about a CI runner's scheduler.

#include <catch2/catch_test_macros.hpp>

// The parser is not ours and is not clean under this project's global /W4, so
// it is included behind a warning barrier -- the same measured treatment
// test_api_schema.cpp gives it.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <nlohmann/json.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "ApiFixture.h"
#include "RawHttpClient.h"

#include "api/ApiPolicy.h"
#include "api/ApiServer.h"
#include "api/ApiSettings.h"

#include "measure/SnapshotSource.h"

#include <memory>
#include <string>
#include <utility>

using rta::api::ApiServer;
using rta::api::ApiSettings;
using rta::api::etagFor;
using rta::api::test::hasHeader;
using rta::api::test::headerValue;
using rta::api::test::makeApiFixture;
using rta::api::test::RawResponse;
using rta::api::test::sendRaw;

namespace {

/// The fixture's own sequence, which is also its ETag. ApiFixture.h sets
/// 12345 so a 32-bit truncation anywhere would be visible rather than
/// plausible; the conditional-GET cases below read it back off the wire
/// rather than assuming it.
constexpr std::uint64_t kFixtureSequence = 12345;

/// An ephemeral bind, which is what every case here uses: a fixed 4736 would
/// collide with a developer's own running instance and with the other cases
/// in this file, and the plan's I10 loop asserts that a fresh bind succeeds
/// ten times in a row.
[[nodiscard]] ApiSettings ephemeral() {
    ApiSettings settings;
    settings.enabled = true;
    settings.port = 0;
    return settings;
}

/// A server and the snapshot it reads, with the source declared FIRST so it
/// outlives the server -- the same declaration-order argument the
/// composition root makes for `apiServer_` after `analysisThread_`.
class Fixture {
public:
    explicit Fixture(ApiSettings settings)
        : source_(std::make_shared<const rta::measure::Snapshot>(makeApiFixture())),
          server_(std::make_unique<ApiServer>(source_, std::move(settings))) {}

    [[nodiscard]] ApiServer& server() { return *server_; }
    [[nodiscard]] int port() const { return server_->boundPort(); }

private:
    rta::measure::StaticSnapshotSource source_;
    std::unique_ptr<ApiServer> server_;
};

/// A request line and header block, spelled out. `Connection: close` on every
/// one is what makes RawHttpClient's "read until the peer closes" exactly one
/// response, with no keep-alive accounting in the test client.
struct Wire {
    std::string method = "GET";
    std::string target = "/api/v1/status";
    std::string host;    // empty => the correct 127.0.0.1:<port>
    std::string extra;   // extra header lines, each already CRLF-terminated
    std::string body;
    long long declaredLength = -1;   // -1 => body.size()
};

[[nodiscard]] std::string compose(const Wire& wire, int port) {
    const std::string host = wire.host.empty()
                                 ? "127.0.0.1:" + std::to_string(port)
                                 : wire.host;
    std::string out = wire.method + ' ' + wire.target + " HTTP/1.1\r\n";
    out += "Host: " + host + "\r\n";
    out += "Connection: close\r\n";
    const long long length =
        wire.declaredLength >= 0 ? wire.declaredLength
                                 : static_cast<long long>(wire.body.size());
    if (length > 0) {
        out += "Content-Length: " + std::to_string(length) + "\r\n";
    }
    out += wire.extra;
    out += "\r\n";
    out += wire.body;
    return out;
}

[[nodiscard]] RawResponse call(int port, const Wire& wire) {
    return sendRaw(port, compose(wire, port));
}

}  // namespace

// --- I1 / I1b: what the constructor did, asserted on the object ------------

TEST_CASE("I1 a disabled ApiServer binds nothing and reports no port") {
    Fixture fixture(ApiSettings{});   // enabled == false, the shipped default
    CHECK_FALSE(fixture.server().running());
    CHECK(fixture.port() == -1);
}

TEST_CASE("I1b the bound port is knowable the moment the constructor returns") {
    // The case that would have caught the first draft's belief that
    // `bind_to_port` hands the port back: it returns bool, and `Server` has no
    // `port()` accessor at all (that member is on `Client`). Only
    // `bind_to_any_port` returns the port -- httplib.h:2267-2268.
    Fixture fixture(ephemeral());
    CHECK(fixture.port() > 0);
    CHECK(fixture.server().running());
}

// --- I2: one real request over loopback ------------------------------------

TEST_CASE("I2 a GET over loopback serves parseable JSON with the snapshot ETag") {
    Fixture fixture(ephemeral());
    const auto response = call(fixture.port(), Wire{});
    REQUIRE(response.connected);
    REQUIRE(response.status == 200);

    const auto parsed = nlohmann::json::parse(response.body);
    CHECK(parsed.at("schemaVersion").get<int>() == 1);
    CHECK(parsed.at("sequence").get<std::uint64_t>() == kFixtureSequence);
    CHECK(parsed.at("available").size() == 6);
    CHECK(headerValue(response, "ETag") == etagFor(kFixtureSequence));
    CHECK(headerValue(response, "Content-Type").rfind("application/json", 0) == 0);
}

TEST_CASE("I2b every one of the eight routes answers, and an unknown one does not") {
    Fixture fixture(ephemeral());
    for (const char* path : {"status", "snapshot", "transfer", "mtw", "bands", "spectrum",
                             "average", "positions"}) {
        Wire wire;
        wire.target = std::string("/api/v1/") + path;
        const auto response = call(fixture.port(), wire);
        CHECK(response.status == 200);
        CHECK_NOTHROW(nlohmann::json::parse(response.body));
    }
    Wire unknown;
    unknown.target = "/api/v1/nope";
    CHECK(call(fixture.port(), unknown).status == 404);
}

// --- I3: the method boundary, over the wire --------------------------------

TEST_CASE("I3 the method allowlist is real over the wire") {
    Fixture fixture(ephemeral());
    Wire post;
    post.method = "POST";
    const auto refused = call(fixture.port(), post);
    CHECK(refused.status == 405);
    CHECK(headerValue(refused, "Allow") == "GET, HEAD, OPTIONS");

    Wire head;
    head.method = "HEAD";
    const auto served = call(fixture.port(), head);
    CHECK(served.status == 200);
    CHECK(served.body.empty());
}

// --- I4 / I5: the Host check, and that it runs BEFORE routing --------------

TEST_CASE("I4 a forged Host is refused on a valid path") {
    Fixture fixture(ephemeral());
    Wire wire;
    wire.host = "attacker.example:" + std::to_string(fixture.port());
    const auto response = call(fixture.port(), wire);
    CHECK(response.status == 403);
    CHECK(response.body.find("schemaVersion") == std::string::npos);
}

TEST_CASE("I5 the Host check runs BEFORE routing -- a forged Host on an unknown path is 403") {
    // The case that tells presence from ORDER, which is what sec.9 control 1
    // actually says ("before any handler runs"). Pre-routing gives 403; a
    // per-handler check gives 404, because routing ran first and found no
    // route to check anything in.
    Fixture fixture(ephemeral());
    Wire wire;
    wire.target = "/api/v1/nope";
    wire.host = "attacker.example:" + std::to_string(fixture.port());
    CHECK(call(fixture.port(), wire).status == 403);

    // And the same unknown path with the CORRECT Host is a 404, so the 403
    // above is the Host check and not a blanket refusal of unknown paths.
    Wire honest;
    honest.target = "/api/v1/nope";
    CHECK(call(fixture.port(), honest).status == 404);
}

// --- I6: the Bearer path, at a NON-default setting -------------------------

TEST_CASE("I6 the token path executes -- the one server case the shipped default disables") {
    // sec.14 q.4 ships `token` empty, so this control is off by default. A
    // suite that only ever used the default would never execute it: see
    // memory/a-fixed-defect-returns-through-the-silent-fallback.md.
    ApiSettings settings = ephemeral();
    settings.token = "s3cr3t";
    Fixture fixture(settings);
    const int port = fixture.port();

    CHECK(call(port, Wire{}).status == 401);

    Wire wrong;
    wrong.extra = "Authorization: Bearer wrong\r\n";
    CHECK(call(port, wrong).status == 401);

    Wire right;
    right.extra = "Authorization: Bearer s3cr3t\r\n";
    CHECK(call(port, right).status == 200);

    // A query parameter lands in every log that records a URL, and a cookie is
    // ambient authority a browser attaches whichever page issued it. Neither
    // is a parameter `bearerAccepted` can even see, and these two cases are
    // what says so from outside the process.
    Wire query;
    query.target = "/api/v1/status?token=s3cr3t";
    CHECK(call(port, query).status == 401);

    Wire cookie;
    cookie.extra = "Cookie: token=s3cr3t\r\n";
    CHECK(call(port, cookie).status == 401);
}

// --- I7: the CORS posture, asserted rather than assumed --------------------

TEST_CASE("I7 no CORS header is emitted, even with an Origin on the request") {
    Fixture fixture(ephemeral());
    Wire wire;
    wire.extra = "Origin: https://evil.example\r\n";
    const auto response = call(fixture.port(), wire);
    REQUIRE(response.status == 200);
    CHECK_FALSE(hasHeader(response, "Access-Control-Allow-Origin"));
    CHECK_FALSE(hasHeader(response, "Access-Control-Allow-Credentials"));
    CHECK_FALSE(hasHeader(response, "Vary"));
}

// --- I8: the conditional GET round trip ------------------------------------

TEST_CASE("I8 If-None-Match with the served ETag is 304 with no body") {
    Fixture fixture(ephemeral());
    const int port = fixture.port();
    const auto first = call(port, Wire{});
    REQUIRE(first.status == 200);
    const std::string etag = headerValue(first, "ETag");
    REQUIRE_FALSE(etag.empty());

    Wire conditional;
    conditional.extra = "If-None-Match: " + etag + "\r\n";
    const auto second = call(port, conditional);
    CHECK(second.status == 304);
    CHECK(second.body.empty());
    CHECK(headerValue(second, "ETag") == etag);
}

// --- I9: 413, refused before the handler ran -------------------------------

TEST_CASE("I9 a body over maxRequestBodyBytes is 413 and no handler runs") {
    ApiSettings settings = ephemeral();
    settings.maxRequestBodyBytes = 16;
    Fixture fixture(settings);

    Wire wire;
    wire.body = std::string(64, 'x');
    const auto response = call(fixture.port(), wire);
    CHECK(response.status == 413);
    CHECK(response.body.find("schemaVersion") == std::string::npos);

    // And the same server still serves a request that carries no body, so the
    // 413 is the cap and not a server that has stopped working.
    CHECK(call(fixture.port(), Wire{}).status == 200);
}

// --- I10: shutdown and port reuse, ten times -------------------------------

TEST_CASE("I10 construct, start, stop and destroy ten times over") {
    // One of the two behaviours that differ most across Windows, Linux and
    // macOS sockets, and after sec.15 R15 it runs on all three.
    for (int i = 0; i < 10; ++i) {
        Fixture fixture(ephemeral());
        REQUIRE(fixture.port() > 0);
        CHECK(call(fixture.port(), Wire{}).status == 200);
    }
}

// --- I11: no WebSocket can be established, and the reason is measured ------

TEST_CASE("I11 a WebSocket upgrade offer is answered as an ordinary GET") {
    // An upgrade IS a GET (httplib.h:5481 returns false for any other
    // method), so the method allowlist passes it and can never answer one
    // with 405 -- the first draft of the plan said 405 and was wrong (D4).
    // What makes an upgrade impossible is that ApiServer registers eight
    // `Get` routes and no `WebSocket` handler, so `websocket_handlers_` is
    // empty and control falls through to ordinary routing.
    Fixture fixture(ephemeral());
    const std::string upgrade =
        "Upgrade: websocket\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n";

    Wire known;
    // `Connection: close` is already on every request compose() builds, and
    // an upgrade needs `Connection: Upgrade` -- both tokens, on one field,
    // which is what RFC 9110 sec.7.8 means by a comma-separated list and what
    // httplib's `has_header_token` matches against.
    known.extra = "Connection: Upgrade\r\n" + upgrade;
    const auto onKnown = call(fixture.port(), known);
    CHECK(onKnown.status == 200);
    CHECK(onKnown.status != 101);
    CHECK_FALSE(hasHeader(onKnown, "Sec-WebSocket-Accept"));
    CHECK_NOTHROW(nlohmann::json::parse(onKnown.body));

    Wire unknown;
    unknown.target = "/api/v1/nope";
    unknown.extra = "Connection: Upgrade\r\n" + upgrade;
    const auto onUnknown = call(fixture.port(), unknown);
    CHECK(onUnknown.status == 404);
    CHECK_FALSE(hasHeader(onUnknown, "Sec-WebSocket-Accept"));
}

// --- the rate limiter, over the wire ---------------------------------------

TEST_CASE("the rate limit is a hard bound on requests per second over the wire") {
    // The limiter runs BEFORE any `latest()`, which is what makes it a
    // real-time-safety control rather than hygiene (sec.4). Set to 3 rather
    // than the shipped 30 so the case needs four requests instead of
    // thirty-one -- the sliding window is `RateLimiter`'s own subject in
    // test_api_limits.cpp, and what this case proves is that the server
    // consults it at all and answers 429.
    ApiSettings settings = ephemeral();
    settings.maxRequestsPerSecond = 3;
    Fixture fixture(settings);
    const int port = fixture.port();

    CHECK(call(port, Wire{}).status == 200);
    CHECK(call(port, Wire{}).status == 200);
    CHECK(call(port, Wire{}).status == 200);
    CHECK(call(port, Wire{}).status == 429);
}
