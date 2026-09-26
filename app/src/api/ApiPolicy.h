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

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>

namespace rta::api {

/// The four methods this API can see. `Other` is everything else, and it is
/// one value rather than an enumeration of verbs because the allowlist is
/// what matters -- a new verb invented by a client is refused by being
/// unknown, not by being listed.
enum class Method { Get, Head, Options, Other };

/// What a conditional GET resolves to. `NotModified` is 304 with the same
/// `ETag` and no body; `Serve` is the ordinary 200 path.
enum class Verdict { Serve, NotModified };

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

/// The `ETag` for a snapshot, quotes INCLUDED: an HTTP entity tag is a
/// quoted string (RFC 9110 sec.8.8.3), and a bare `12345` is a malformed
/// validator a conforming client will not echo -- which degrades every
/// conditional GET to a full response, silently, forever.
[[nodiscard]] std::string etagFor(std::uint64_t sequence);

/// Record sec.3's polling contract as a pure function. `If-None-Match` is
/// the primary form and wins when both are present; `?since=` is the form
/// that needs no server-issued validator.
///
/// A client whose `since` is AHEAD of the server is served, not refused: it
/// is a client that restarted, or a server that did, and serving is the only
/// recovery from either.
[[nodiscard]] Verdict conditionalVerdict(std::string_view ifNoneMatch,
                                         std::optional<std::uint64_t> since,
                                         std::uint64_t sequence);

// --- LOW follow-up batch, item 12: the pre-routing SEQUENCING, pulled out of
// ApiServer.cpp's installPreRouting() so this exact ordering (controls 1-5)
// is provable in RTA_BUILD_APP=OFF, not only through a real socket in
// app/tests_juce/test_api_server*.cpp. That file keeps only the two things a
// pure function cannot own: reading the live httplib::Request into the plain
// PreRoutingInputs below, and the rate limiter (control 6 -- real mutable
// state and a clock).

/// What `decidePreRoutingRefusal` found wrong with a request, or `None` when
/// every one of controls 1-5 passed and the caller may proceed to the rate
/// limiter.
enum class PreRoutingRefusal {
    None,
    MultipleHostHeaders,
    HostNotAllowed,
    MethodNotAllowed,
    Unauthorized,
    BodyTooLarge,
};

/// Everything `decidePreRoutingRefusal` needs, as ALREADY-DECODED plain
/// values -- this file's whole reason to exist (record sec.15 R1) is that
/// nothing here may see an `httplib::Request`, so the ONE httplib-touching
/// caller (`ApiServer::Impl::installPreRouting`) extracts these fields
/// itself before calling in.
struct PreRoutingInputs {
    /// `request.get_header_value_count("Host")` -- RFC 9112 sec.3.2 requires
    /// exactly one; `get_header_value` alone would silently read only the
    /// FIRST of several.
    int hostHeaderCount = 0;
    std::string_view hostHeaderValue;
    /// The port actually bound -- see `hostIsAllowed`'s own comment for why
    /// this is not always `settings` port (an ephemeral bind).
    int boundPort = -1;
    Method method = Method::Other;
    std::string_view authorizationHeader;
    /// The SAME value `bodyIsAcceptable`'s own first argument takes.
    long long declaredBodyBytes = 0;
};

/// Record sec.9 controls 1-5, decided IN THIS ORDER -- the order IS the
/// decision (PR #18's first version had the rate limiter, control 6, running
/// FIRST instead of last, and the station-5 verifier measured the cost: three
/// forged-Host requests at a limit of 3 filled the window and a legitimate
/// fourth request got 429 -- a caller outside the allowlist, with no token,
/// could deny the API to the operator during a show).
///
///  1. MORE THAN ONE Host header -> refused, before either is even read.
///  2. The Host allowlist -> refused BEFORE any other control runs. The
///     highest-value control in the whole API: a forged Host on a path that
///     does not exist must refuse here, never fall through to a 404.
///  3. The method allowlist -> refused. The `Allow` header value is the
///     CALLER's job (`kAllowedMethods`, spelled once in ApiServer.cpp); this
///     returns only the refusal.
///  4. The Bearer token, when `settings.token` is non-empty.
///  5. The body cap, refused BEFORE the body is read -- what makes sec.9's
///     415 unreachable rather than merely unimplemented.
///
/// The rate limiter (control 6) is deliberately NOT decided here:
/// `RateLimiter::admit` mutates shared state and needs a clock reading, so it
/// is not a pure decision the same way these five are -- see
/// `ApiServer::Impl::installPreRouting` for where it runs (last, immediately
/// before routing, only once every refusal here has already passed) and why.
[[nodiscard]] PreRoutingRefusal decidePreRoutingRefusal(const PreRoutingInputs& in,
                                                         const ApiSettings& settings);

/// A SLIDING window, and the choice is load-bearing (record sec.4, sec.15
/// R15's neighbourhood). A fixed window admits `maxPerSecond` at the end of
/// one window and `maxPerSecond` at the start of the next -- twice the rate
/// inside one real second -- while sec.4's accounting is that this number is
/// a HARD BOUND on the API thread's traffic against the publish slot. A
/// bound that holds only on aligned seconds is not that bound.
///
/// The clock is INJECTED. Nothing here reads the time, so the tests need no
/// sleep and cannot be flaky on a CI runner; `ApiServer` passes
/// `std::chrono::steady_clock::now()`.
///
/// `admit` runs BEFORE any `SnapshotSource::latest()`, which is what makes
/// the limiter a real-time-safety control rather than hygiene: the expensive
/// work is what it is bounding, so it cannot run after it.
class RateLimiter {
public:
    using Clock = std::chrono::steady_clock;

    explicit RateLimiter(int maxPerSecond);

    /// False => 429, and the caller must not touch the publish slot.
    [[nodiscard]] bool admit(Clock::time_point now);

private:
    /// The admission instants still inside the window, oldest first. Bounded
    /// by `maxPerSecond_` by construction -- an entry is only ever pushed
    /// after the size check passes -- so this never grows with traffic.
    std::deque<Clock::time_point> admissions_;
    int maxPerSecond_;
};

}  // namespace rta::api
