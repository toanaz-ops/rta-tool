// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/api. No JUCE, no Qt, no audio-device API, and
// no server library.
// See docs/dsp/2026-09-16-remote-api.md sec.8 as amended by sec.15 (R5, R14,
// R17), and sec.14 q.1/q.2/q.4 whose defaults this file makes concrete.
#pragma once

#include <string>
#include <vector>

namespace rta::api {

/// The shipped point cap, named so `Request`'s own default can be the same
/// number without constructing an `ApiSettings` to read it. One constant,
/// two users -- a cap spelled twice is a cap that can disagree with itself.
inline constexpr int kDefaultMaxPointsPerResponse = 8192;

/// The API's settings, as record sec.8 lists them except where sec.15 amends
/// sec.8. A PLAIN STRUCT and nothing more: sec.15 R5 records that no
/// preferences store exists anywhere in `app/`, so v1 persists nothing and
/// the composition root constructs this by hand. Building a preferences
/// store to hold eight fields would be a second decision smuggled in behind
/// a first one.
struct ApiSettings {
    /// Record sec.8: off until the operator asks. Nothing observable changes
    /// for an operator who did not ask for the API.
    bool enabled = false;

    /// A LITERAL IP, never the string "localhost". cpp-httplib's own README
    /// warns that resolving it on Windows with misconfigured IPv6 can cost up
    /// to 2 s per request -- a two-second stall on a 20 Hz poll during a show.
    /// This is the BIND address; the `Host` header allowlist is a different
    /// question with a different answer (see hostIsAllowed, which does accept
    /// the name `localhost` because the header is the requested name).
    std::string bindAddress = "127.0.0.1";

    /// Record sec.15 R14. NOT sec.8's proposed 4737: the IANA Service Name
    /// and Transport Protocol Port Number Registry has `ipdr-sp,4737,tcp`
    /// and `ipdr-sp,4737,udp` (registered 2005-08), while 4734, 4735 (REW's
    /// own) and 4736 are absent from it. User Ports are not exclusive so
    /// 4737 would have worked -- but a named default resting on a check
    /// nobody ran is what this project's method exists to prevent.
    int port = 4736;

    /// Empty = no token required (record sec.14 q.4's default). When
    /// non-empty it is checked as `Authorization: Bearer <token>`, NEVER a
    /// cookie and never a query parameter that lands in a log.
    ///
    /// Because the shipped default DISABLES this control, it is the one
    /// thing in the lane that must be tested with a non-default setting --
    /// memory/a-fixed-defect-returns-through-the-silent-fallback.md. See
    /// test_api_policy.cpp B7-B9.
    std::string token{};

    /// Record sec.8, and it is a real-time-safety control rather than
    /// hygiene: this is the rate the sec.4 accounting of the API thread's
    /// traffic against the publish slot is stated at.
    int maxRequestsPerSecond = 30;

    /// Clamps any caller-supplied `?points=`. The second way a remote caller
    /// can make this program do unbounded work while a show is running.
    int maxPointsPerResponse = kDefaultMaxPointsPerResponse;

    /// Record sec.15 R17. A GET should carry no body at all; anything over
    /// this is refused with 413 BEFORE routing, which is what makes 415
    /// unreachable rather than merely unimplemented.
    long long maxRequestBodyBytes = 8192;

    /// Empty = no CORS headers emitted, and their absence is NOT a defence:
    /// a GET with only safelisted headers is a SIMPLE request, gets no
    /// preflight, and is EXECUTED by this program before the browser decides
    /// whether the calling script may read the reply (record sec.9).
    std::vector<std::string> corsOrigins{};

    /// Record sec.14 q.2's default: the setting is PRESENT and the server
    /// REFUSES to start when it is true (see startRefusal). Honest about the
    /// roadmap, and impossible to turn on by accident because the code path
    /// behind it does not exist.
    bool allowLanBind = false;
};

}  // namespace rta::api
