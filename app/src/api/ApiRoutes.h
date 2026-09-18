// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/api. No JUCE, no Qt, no audio-device API, and
// no server library: this is the endpoint TABLE, not the socket.
// See docs/dsp/2026-09-16-remote-api.md sec.6, sec.15 R12/R13.
#pragma once

#include "api/ApiSerialise.h"
#include "api/ApiSettings.h"

#include "measure/Snapshot.h"

#include <cstddef>
#include <span>
#include <string>

namespace rta::api {

/// What the eight endpoints have in common once the differing signatures are
/// adapted: a snapshot, a validated request shape, and the settings (only
/// `/status` reads them, and it reads them to answer `available`).
///
/// A PLAIN FUNCTION POINTER, not a `std::function`. There is nothing to
/// capture -- which is the point: a captured `&settings` would put a lifetime
/// question into a table that has none, and the table is then a compile-time
/// constant rather than eight heap-allocated closures built at start-up.
using Serialiser = std::string (*)(const measure::Snapshot&, const Request&,
                                   const ApiSettings&);

struct RouteEntry {
    /// The path segment under `/api/v1/`, with no slash of its own.
    const char* path;
    Serialiser body;
};

/// The eight endpoints of sec.15 R12 -- EIGHT, not the six names in
/// `/status`'s `available`, which is a capability list and not an endpoint
/// count.
///
/// WHY THIS IS A TABLE IN ITS OWN TRANSLATION UNIT. `ApiServer.cpp` is the
/// one file in the repository permitted to include `<httplib.h>`
/// (`core/tests/check_no_server_library.cmake`), so everything that lands
/// there is paid for in that file's length budget and can be tested only
/// through a socket. The plan named this seam in advance -- Task I's "the
/// seam if it grows is validation-versus-routing" -- and this is the routing
/// half: which paths exist and what each one serialises, asserted directly
/// in the RTA_BUILD_APP=OFF target with no server in the picture.
///
/// The span points at a function-local static, so it is valid for the
/// lifetime of the program and the same object on every call.
///
/// ## Which methods each resource answers, and why it is exactly three
///
/// `ApiPolicy::methodIsAllowed` permits **GET, HEAD, OPTIONS**, and
/// `ApiServer` advertises that same string on a 405. All three are SERVED,
/// which is the part PR #18 got wrong: it advertised OPTIONS and registered
/// `Get` only, so OPTIONS passed the allowlist, found no route and answered
/// **404 with no `Allow` at all** -- an API naming a method it does not
/// serve, which is worse than one naming fewer. The two ways out were "serve
/// it" and "delete it from `methodIsAllowed` and from the `Allow` string";
/// serving it is the smaller change and the more useful surface, and it is
/// recorded as the sec.8 amendment in the decision record.
///
/// **GET** -- the eight entries above. One `SnapshotSource::latest()`, 200 with
/// an `ETag`, 304 on a matching validator, **503 before the first publish**:
/// "no measurement exists" and "a measurement of nothing" are different
/// answers and a client that cannot tell them apart will plot the second.
///
/// **HEAD** -- needs no registration, and that is not an omission. httplib
/// dispatches GET and HEAD to the same `get_handlers_` (`Server::routing`,
/// `httplib.h:13973`) and suppresses the body on the way out, so each `Get`
/// above serves both, `ETag` intact -- which makes HEAD + `If-None-Match` the
/// cheapest form of sec.3's polling contract.
/// `app/tests/test_api_server_refusals.cpp` asserts that on all eight routes
/// rather than leaving it to this reading of somebody else's code.
///
/// **OPTIONS** -- `204` with `Allow`, and **no snapshot is read**. OPTIONS
/// describes the RESOURCE, not a measurement, so it must answer identically
/// before the first publish, where a GET correctly answers 503. `204` rather
/// than `200` because there is no representation to return (RFC 9110
/// sec.9.3.7 leaves the body optional); a `200` with an empty body would
/// claim one exists. An unknown path is still 404 for OPTIONS -- the method is
/// served on the eight resources that exist, never as a wildcard.
///
/// Everything else is **405** with `Allow`. Note what that does NOT cover: a
/// WebSocket upgrade is a `GET` (sec.15 R16a), so it passes the allowlist and
/// can never be answered with 405. What makes one impossible is that
/// `ApiServer` registers no `WebSocket` handler -- this table has no entry
/// shape that could express one.
[[nodiscard]] std::span<const RouteEntry> apiRoutes();

}  // namespace rta::api
