// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/api. See ApiPolicy.h for why these live here
// and not in ApiServer.cpp (record sec.15 R1).

#include "api/ApiPolicy.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <string_view>
#include <system_error>

namespace rta::api {
namespace {

/// ASCII-only lowering. Deliberately not `std::tolower`, which takes an
/// `int`, is locale-dependent and has undefined behaviour on a negative
/// `char` -- three ways for a header from outside this program to behave
/// differently on a Turkish locale than on the developer's machine.
[[nodiscard]] constexpr char lowerAscii(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] bool equalsAsciiCaseInsensitive(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lowerAscii(a[i]) != lowerAscii(b[i])) {
            return false;
        }
    }
    return true;
}

/// Parses a port that occupies the WHOLE token. `from_chars` stopping short
/// is a failure here, not a partial success: that is what rejects
/// `127.0.0.1:4736.attacker.example` and `127.0.0.1:4736 ` -- the substring
/// trap and the trailing-whitespace trap, both of which a lenient parse
/// admits.
[[nodiscard]] bool portEquals(std::string_view token, int expected) noexcept {
    int parsed = 0;
    const auto* const end = token.data() + token.size();
    const auto result = std::from_chars(token.data(), end, parsed);
    return result.ec == std::errc{} && result.ptr == end && parsed == expected;
}

/// The bind addresses v1 will actually bind. Loopback LITERALS only: the
/// name `localhost` is excluded here even though `hostIsAllowed` accepts it
/// in the header, and the asymmetry is the point -- resolving `localhost`
/// with misconfigured IPv6 on Windows costs up to 2 s per request, which is
/// a stall a 20 Hz poll cannot absorb during a show.
[[nodiscard]] bool isLoopbackBindAddress(std::string_view address) noexcept {
    return address == "127.0.0.1" || address == "::1" || address == "[::1]";
}

}  // namespace

bool hostIsAllowed(std::string_view host, int port) {
    // An IPv6 literal carries its own colons, so the host/port split is at
    // "]:" for a bracketed form and at the LAST ':' otherwise. Getting this
    // wrong on "[::1]:4736" would split inside the address.
    std::string_view name;
    std::string_view portToken;
    if (!host.empty() && host.front() == '[') {
        const auto close = host.find("]:");
        if (close == std::string_view::npos) {
            return false;
        }
        name = host.substr(0, close + 1);
        portToken = host.substr(close + 2);
    } else {
        const auto colon = host.rfind(':');
        if (colon == std::string_view::npos) {
            return false;   // no port at all: a Host with no port is not one of ours
        }
        name = host.substr(0, colon);
        portToken = host.substr(colon + 1);
    }

    if (!portEquals(portToken, port)) {
        return false;
    }

    // The three forms NCC Group's Singularity guidance prescribes, matched
    // whole. `localhost` is case-insensitive per RFC 4343; the two literals
    // have no case to speak of but go through the same comparison so a
    // future addition cannot pick the wrong one by accident.
    static constexpr std::array<std::string_view, 3> kAllowed{"127.0.0.1", "localhost", "[::1]"};
    for (const auto allowed : kAllowed) {
        if (equalsAsciiCaseInsensitive(name, allowed)) {
            return true;
        }
    }
    return false;
}

Method methodOf(std::string_view verb) {
    if (verb == "GET") {
        return Method::Get;
    }
    if (verb == "HEAD") {
        return Method::Head;
    }
    if (verb == "OPTIONS") {
        return Method::Options;
    }
    return Method::Other;
}

bool methodIsAllowed(Method m) {
    return m == Method::Get || m == Method::Head || m == Method::Options;
}

bool bearerAccepted(std::string_view authorizationHeader, const ApiSettings& settings) {
    if (settings.token.empty()) {
        return true;   // the shipped default: this control is off by design
    }

    // RFC 9110 sec.11.1: the auth SCHEME token is case-insensitive. Exactly
    // one space separates it from the credential, and the credential is
    // compared byte-for-byte -- "Bearer  s3cr3t" (two spaces) and
    // "Bearer s3cr3t " (trailing) are both wrong credentials, not sloppy
    // spellings of a right one.
    static constexpr std::string_view kScheme = "Bearer ";
    if (authorizationHeader.size() <= kScheme.size()) {
        return false;
    }
    if (!equalsAsciiCaseInsensitive(authorizationHeader.substr(0, kScheme.size()), kScheme)) {
        return false;
    }
    return authorizationHeader.substr(kScheme.size()) == settings.token;
}

int clampPoints(long long requested, const ApiSettings& settings) {
    const long long limit = settings.maxPointsPerResponse;
    if (requested <= 0 || requested > limit) {
        return settings.maxPointsPerResponse;
    }
    return static_cast<int>(requested);
}

bool bodyIsAcceptable(long long bodyBytes, const ApiSettings& settings) {
    return bodyBytes >= 0 && bodyBytes <= settings.maxRequestBodyBytes;
}

std::optional<std::string> startRefusal(const ApiSettings& settings) {
    if (settings.allowLanBind) {
        return std::string(
            "api.allowLanBind is true, and v1 has no LAN bind: the setting exists so the "
            "roadmap is visible, and the code path behind it does not. A LAN listener needs "
            "the mandatory password that must ship with it (record sec.13).");
    }
    if (!isLoopbackBindAddress(settings.bindAddress)) {
        return std::string(
            "api.bindAddress is not a loopback literal, and v1 binds loopback only. Use "
            "127.0.0.1 or ::1 -- not the name 'localhost', whose resolution can cost up to "
            "2 s per request on Windows with misconfigured IPv6.");
    }
    return std::nullopt;
}

std::string etagFor(std::uint64_t sequence) {
    return '"' + std::to_string(sequence) + '"';
}

Verdict conditionalVerdict(std::string_view ifNoneMatch, std::optional<std::uint64_t> since,
                           std::uint64_t sequence) {
    // If-None-Match is the primary form and DECIDES when present, agreeing
    // with ?since= or not: a client that sends a validator has one, and the
    // validator is the thing the standard defines the semantics of.
    if (!ifNoneMatch.empty()) {
        // RFC 9110 sec.13.1.2: `*` matches any current representation.
        if (ifNoneMatch == "*") {
            return Verdict::NotModified;
        }
        return ifNoneMatch == etagFor(sequence) ? Verdict::NotModified : Verdict::Serve;
    }

    if (since.has_value()) {
        // Equality only. A `since` AHEAD of the server means the client (or
        // the server) restarted; serving is the only recovery, so anything
        // that is not "you already have exactly this one" is served.
        return *since == sequence ? Verdict::NotModified : Verdict::Serve;
    }
    return Verdict::Serve;
}

RateLimiter::RateLimiter(int maxPerSecond) : maxPerSecond_(maxPerSecond) {}

bool RateLimiter::admit(Clock::time_point now) {
    if (maxPerSecond_ <= 0) {
        return false;   // a limiter configured to admit nothing admits nothing
    }

    constexpr auto kWindow = std::chrono::seconds{1};

    // `>=` is the shipped comparison and test C3's t0+1000ms case is what
    // says so: an admission exactly one window old has left the window. The
    // alternative (`>`) keeps it for one more nanosecond, which is a
    // different bound, and an untested one either way.
    while (!admissions_.empty() && (now - admissions_.front()) >= kWindow) {
        admissions_.pop_front();
    }

    if (static_cast<int>(admissions_.size()) >= maxPerSecond_) {
        return false;
    }
    admissions_.push_back(now);
    return true;
}

}  // namespace rta::api
