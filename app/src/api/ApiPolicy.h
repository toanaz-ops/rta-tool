// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/api. No JUCE, no Qt, no audio-device API, and
// NO SERVER LIBRARY: record sec.15 R1 exists because sec.10 put these
// functions inside the ON-only httplib TU while sec.11 items 6-9 require
// them proven in RTA_BUILD_APP=OFF. Both could not hold, so the validation
// half moved here and `ApiServer.cpp` keeps only the socket, the thread and
// the routing.
// See docs/dsp/2026-09-16-remote-api.md sec.3, sec.8, sec.9, sec.11.
#pragma once

#include "api/ApiSettings.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rta::api {

/// The four methods this API can see. `Other` is everything else, and it is
/// one value rather than an enumeration of verbs because the allowlist is
/// what matters -- a new verb invented by a client is refused by being
/// unknown, not by being listed.
enum class Method { Get, Head, Options, Other };

/// Record sec.9 control 1. The `Host` header names a host and a port, and
/// BOTH must match, because this is the DNS-rebinding defence: an attacker
/// page that has rebound its own name to 127.0.0.1 still sends
/// `Host: attacker.example`, since the header carries the REQUESTED name and
/// never the resolved address.
///
/// `localhost` is accepted and that is deliberate (RFC 4343: a hostname is
/// case-insensitive, so `LOCALHOST` is the same name). It costs nothing
/// against rebinding, per NCC Group's Singularity guidance, which prescribes
/// exactly `127.0.0.1:<port>` and/or `localhost:<port>`. Do not "harden"
/// this by dropping it.
///
/// The match is on the WHOLE header value, never a substring: a naive
/// `find("127.0.0.1:4736")` accepts `127.0.0.1:4736.attacker.example`, which
/// is the trap the "strictly contain" wording exists to catch.
[[nodiscard]] bool hostIsAllowed(std::string_view host, int port);

/// Exact, case-SENSITIVE match. The HTTP method token is case-sensitive
/// (RFC 9110 sec.9.1), so `get` is not `GET` and is refused as unknown.
[[nodiscard]] Method methodOf(std::string_view verb);

/// Record sec.9 control 2 -> 405 with an `Allow` header. Note what this does
/// NOT do: a WebSocket upgrade is `GET /path HTTP/1.1` carrying
/// `Upgrade: websocket`, so it PASSES this check (record sec.15 R16a). What
/// makes an upgrade impossible is that no handler registers one.
[[nodiscard]] bool methodIsAllowed(Method m);

/// Record sec.9 control 4 -> 401. Reads the `Authorization` header and the
/// settings, and NOTHING ELSE -- there is no parameter a cookie or a query
/// string could arrive through, and that absence is the control.
///
/// A cookie is ambient authority: a browser attaches it to every request to
/// `127.0.0.1:<port>` whichever page issued it, so any site the operator
/// opens during a show would be authenticated to this listener. A query
/// parameter is worse again, because it lands in every log that records a
/// URL.
///
/// Returns TRUE when `settings.token` is empty -- the shipped default, where
/// this control is off by design and the defence is the `Host` allowlist
/// plus the loopback bind.
[[nodiscard]] bool bearerAccepted(std::string_view authorizationHeader,
                                  const ApiSettings& settings);

/// Record sec.11 item 6. `requested` is `long long` precisely so a hostile
/// `?points=` beyond `INT_MAX` CLAMPS rather than wrapping: signed overflow
/// is undefined behaviour, and a wrap would turn a huge request into a
/// negative count. Absent or garbage (<= 0) means "as much as allowed",
/// never zero: a client that omits the parameter wants the whole curve, and
/// an empty body would be a silent truncation nobody asked for.
[[nodiscard]] int clampPoints(long long requested, const ApiSettings& settings);

/// Record sec.15 R17 -> 413, refused BEFORE routing. A GET should carry no
/// body at all; this cap is what makes sec.9's 415 unreachable rather than
/// unimplemented, which is why 415 is dropped rather than listed and absent.
[[nodiscard]] bool bodyIsAcceptable(long long bodyBytes, const ApiSettings& settings);

/// Record sec.14 q.2's default, made visible: `allowLanBind` EXISTS as a
/// setting and the code path behind it does not. Returns a named refusal
/// when the settings ask for something v1 will not do -- a LAN bind by the
/// flag, or by a bind address that is not loopback -- and `nullopt` when the
/// server may start.
///
/// Two ways to ask for a LAN bind, one refusal. A setting that silently did
/// nothing, or silently did it, are the two failure modes this refuses.
[[nodiscard]] std::optional<std::string> startRefusal(const ApiSettings& settings);

}  // namespace rta::api
