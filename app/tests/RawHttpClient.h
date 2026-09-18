// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A raw-socket HTTP client for test_api_server.cpp: fixed request strings in,
// a status line and a header block out. No parsing of its own, no keep-alive,
// no timeouts, no redirects, no security surface.
//
// WHY THIS AND NOT `httplib::Client` (plan Task I, V2's second-order note).
// `core/tests/check_no_server_library.cmake` permits exactly ONE file in the
// repository to include <httplib.h> -- app/src/api/ApiServer.cpp -- and a
// client built on httplib would be a second. Making ALLOW a list weakens the
// guard's headline claim from "exactly one" to "two" and makes its second
// sentinel ambiguous about which file must still carry the include. Testing a
// pure `respond(RequestView)` seam instead would keep the guard intact and
// test no socket at all, which is the entire point of these cases.
//
// THIS IS NOT THE HAND-ROLLING RECORD sec.2 REJECTED. That paragraph rejected
// hand-rolling a SERVER, and listed the machinery it would need: request
// parsing, chunked transfer, keep-alive accounting, header folding, a thread
// pool, timeout handling. A client that writes a fixed string and reads until
// the peer closes contains none of it.
#pragma once

#if defined(_WIN32)
// winsock2.h must precede any windows.h, which is why nothing above this line
// includes a Windows header.
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace rta::api::test {

#if defined(_WIN32)
using SocketHandle = SOCKET;
inline constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

inline void closeSocket(SocketHandle s) { ::closesocket(s); }

/// One WSAStartup for this translation unit. httplib runs its own inside
/// ApiServer.cpp's TU (`detail::WSInit`) and this is a different TU, so it
/// cannot borrow that one.
inline bool socketsReady() {
    static const bool ready = [] {
        WSADATA data{};
        return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ready;
}
#else
using SocketHandle = int;
inline constexpr SocketHandle kInvalidSocket = -1;

inline void closeSocket(SocketHandle s) { ::close(s); }
inline bool socketsReady() { return true; }
#endif

/// What came back. `reset` records that the peer closed with our unread
/// request body still in its receive buffer, which makes TCP answer with an
/// RST -- the ordinary outcome of the 413 case, where the server refuses
/// BEFORE reading a body it has been told is oversized. Bytes already
/// delivered before the reset are kept and are what the assertions read; a
/// client that treated the reset as a failure would be asserting about the
/// kernel's timing rather than about the response.
struct RawResponse {
    bool connected = false;
    bool reset = false;
    int status = -1;
    std::string headers;   // the block after the status line, CRLF-separated
    std::string body;
};

/// Sends `request` verbatim to 127.0.0.1:`port` and reads until the peer
/// closes. Every request this file is handed carries `Connection: close`, so
/// "until close" is one response and there is no keep-alive accounting here.
[[nodiscard]] inline RawResponse sendRaw(int port, std::string_view request) {
    RawResponse out;
    if (!socketsReady()) {
        return out;
    }

    const SocketHandle sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock == kInvalidSocket) {
        return out;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    if (::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        closeSocket(sock);
        return out;
    }
    if (::connect(sock, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        closeSocket(sock);
        return out;
    }
    out.connected = true;

    std::size_t sent = 0;
    while (sent < request.size()) {
        const auto n =
            ::send(sock, request.data() + sent, static_cast<int>(request.size() - sent), 0);
        if (n <= 0) {
            closeSocket(sock);
            return out;
        }
        sent += static_cast<std::size_t>(n);
    }

    std::string raw;
    char buffer[4096];
    for (;;) {
        const auto n = ::recv(sock, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (n == 0) {
            break;
        }
        if (n < 0) {
            out.reset = true;
            break;
        }
        raw.append(buffer, static_cast<std::size_t>(n));
    }
    closeSocket(sock);

    const auto endOfStatus = raw.find("\r\n");
    if (endOfStatus == std::string::npos) {
        return out;
    }
    const std::string statusLine = raw.substr(0, endOfStatus);
    const auto firstSpace = statusLine.find(' ');
    if (firstSpace != std::string::npos && statusLine.size() >= firstSpace + 4) {
        out.status = std::stoi(statusLine.substr(firstSpace + 1, 3));
    }

    const auto endOfHeaders = raw.find("\r\n\r\n", endOfStatus);
    if (endOfHeaders == std::string::npos) {
        out.headers = raw.substr(endOfStatus + 2);
        return out;
    }
    out.headers = raw.substr(endOfStatus + 2, endOfHeaders - endOfStatus - 2);
    out.body = raw.substr(endOfHeaders + 4);
    return out;
}

namespace detail {

/// ASCII-only lowering, and deliberately not `c | 0x20`: that trick equates
/// CR (0x0D) with '-' (0x2D), and '-' is in every header name these cases
/// look for. Not `std::tolower` either -- locale-dependent, `int`-taking, and
/// undefined on a negative `char`, which is what ApiPolicy.cpp's own
/// `lowerAscii` comment already says about header text from outside.
[[nodiscard]] constexpr char lowerAscii(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/// The field value, or nullopt when the field is absent. Field names are
/// case-insensitive (RFC 9110 sec.5.1), so a test searching for one exact
/// spelling would pass or fail on how httplib happens to capitalise -- which
/// is not what any of these cases is about.
///
/// Absent and present-but-empty stay DISTINCT, which is the distinction the
/// CORS case turns on: `Access-Control-Allow-Origin` must not be there at
/// all, and a helper returning "" for both could not tell those apart.
[[nodiscard]] inline std::optional<std::string> findHeader(const RawResponse& response,
                                                           std::string_view name) {
    const std::string block = "\r\n" + response.headers + "\r\n";
    std::size_t at = 0;
    while ((at = block.find("\r\n", at)) != std::string::npos) {
        const std::size_t lineStart = at + 2;
        at = lineStart;
        const auto colon = block.find(':', lineStart);
        const auto lineEnd = block.find("\r\n", lineStart);
        if (colon == std::string::npos || lineEnd == std::string::npos || colon > lineEnd) {
            continue;
        }
        if (colon - lineStart != name.size()) {
            continue;
        }
        bool same = true;
        for (std::size_t i = 0; i < name.size(); ++i) {
            if (lowerAscii(block[lineStart + i]) != lowerAscii(name[i])) {
                same = false;
                break;
            }
        }
        if (!same) {
            continue;
        }
        std::size_t valueStart = colon + 1;
        while (valueStart < lineEnd && block[valueStart] == ' ') {
            ++valueStart;
        }
        return block.substr(valueStart, lineEnd - valueStart);
    }
    return std::nullopt;
}

}  // namespace detail

[[nodiscard]] inline bool hasHeader(const RawResponse& response, std::string_view name) {
    return detail::findHeader(response, name).has_value();
}

[[nodiscard]] inline std::string headerValue(const RawResponse& response,
                                             std::string_view name) {
    return detail::findHeader(response, name).value_or(std::string{});
}

}  // namespace rta::api::test
