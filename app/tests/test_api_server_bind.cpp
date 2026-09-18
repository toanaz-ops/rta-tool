// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L-API Task I, split from test_api_server.cpp along the seam that made
// that file long: HOW `ApiServer` binds, and WHEN it refuses to, rather than
// what it answers once it has. Two subjects nothing else covers.
//
// WHY THE FIXED-PORT CASE IS HERE AT ALL, and it is a gap the plan did not
// name. Every over-the-wire case in test_api_server.cpp binds with
// `settings.port == 0`, deliberately: a literal 4736 would collide with a
// developer's own running instance and with the neighbouring cases. But that
// leaves the branch the SHIPPED configuration actually takes --
// `bind_to_port(host, port)`, the one that returns `bool` -- exercised by
// nothing, while the branch only a test takes is exercised twelve times. The
// production path being the untested one is the shape of defect this
// project's method exists to catch.

#include <catch2/catch_test_macros.hpp>

#include "ApiFixture.h"
#include "RawHttpClient.h"

#include "api/ApiServer.h"
#include "api/ApiSettings.h"

#include "measure/SnapshotSource.h"

#include <memory>
#include <string>
#include <utility>

using rta::api::ApiServer;
using rta::api::ApiSettings;
using rta::api::test::makeApiFixture;
using rta::api::test::sendRaw;

namespace {

/// A port that was free a moment ago: bound ephemerally, read back, released.
/// Not a literal, and not a guess. There is a window between the release and
/// the re-bind in which something else could take it -- unavoidable for any
/// test of a fixed-port bind, and narrower than picking a number and hoping.
[[nodiscard]] int aPortThatWasFree() {
    rta::measure::StaticSnapshotSource source(
        std::make_shared<const rta::measure::Snapshot>(makeApiFixture()));
    ApiSettings settings;
    settings.enabled = true;
    settings.port = 0;
    const ApiServer probe(source, settings);
    return probe.boundPort();
}

}  // namespace

TEST_CASE("the fixed-port bind branch works, and it is the one that ships") {
    const int port = aPortThatWasFree();
    REQUIRE(port > 0);

    rta::measure::StaticSnapshotSource source(
        std::make_shared<const rta::measure::Snapshot>(makeApiFixture()));
    ApiSettings settings;
    settings.enabled = true;
    settings.port = port;   // NOT zero: bind_to_port, the shipped branch
    const ApiServer server(source, settings);

    REQUIRE(server.boundPort() == port);
    CHECK(server.running());
    CHECK_FALSE(server.refusal().has_value());

    // And the `Host` allowlist agrees with the port it was given, which is
    // the half a fixed port can get wrong in a way an ephemeral one cannot:
    // the header's port must match the bound port exactly.
    const std::string request = "GET /api/v1/status HTTP/1.1\r\nHost: 127.0.0.1:" +
                                std::to_string(port) + "\r\nConnection: close\r\n\r\n";
    CHECK(sendRaw(port, request).status == 200);

    const std::string forged = "GET /api/v1/status HTTP/1.1\r\nHost: 127.0.0.1:1\r\n"
                               "Connection: close\r\n\r\n";
    CHECK(sendRaw(port, forged).status == 403);
}

TEST_CASE("allowLanBind exists and REFUSES -- the setting ships, the code path does not") {
    // Record sec.14 q.2's default, end to end rather than only through
    // `startRefusal`. A setting that silently did nothing and a setting that
    // silently did it are the two failure modes this refuses, and a refusal
    // nobody can read from outside is the first of them.
    rta::measure::StaticSnapshotSource source(
        std::make_shared<const rta::measure::Snapshot>(makeApiFixture()));
    ApiSettings settings;
    settings.enabled = true;
    settings.port = 0;
    settings.allowLanBind = true;

    const ApiServer server(source, settings);
    CHECK_FALSE(server.running());
    CHECK(server.boundPort() == -1);
    REQUIRE(server.refusal().has_value());
    CHECK(server.refusal()->find("allowLanBind") != std::string::npos);
}

TEST_CASE("a bind address that is not a loopback literal is refused too") {
    // Two ways to ask for a LAN bind, one refusal. `0.0.0.0` never reaches a
    // socket call here, so there is no window in which this program is
    // listening on a LAN interface without the mandatory password sec.13 says
    // must come with one.
    rta::measure::StaticSnapshotSource source(
        std::make_shared<const rta::measure::Snapshot>(makeApiFixture()));
    ApiSettings settings;
    settings.enabled = true;
    settings.port = 0;
    settings.bindAddress = "0.0.0.0";

    const ApiServer server(source, settings);
    CHECK_FALSE(server.running());
    CHECK(server.boundPort() == -1);
    REQUIRE(server.refusal().has_value());
    CHECK(server.refusal()->find("bindAddress") != std::string::npos);
}
