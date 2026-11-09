// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/api. No JUCE, no Qt, no audio-device API.
// H8 lane b (master plan row 12): the operator's switch for the read-only
// remote API. Record sec.15 R5 says no preferences store exists, so the
// switch is the process environment, read once at start-up.
#pragma once

#include "api/ApiSettings.h"

#include <algorithm>
#include <functional>
#include <string>

namespace rta::api {

namespace detail {
/// ASCII-only, locale-free (same reason as ApiPolicy.cpp's `lowerAscii`):
/// `std::tolower` / `std::isdigit` depend on the C locale and take an `int`
/// that must not be a negative `char` (H8 review L-3).
[[nodiscard]] constexpr char lowerAscii(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}
[[nodiscard]] constexpr bool isAsciiDigit(char c) noexcept { return c >= '0' && c <= '9'; }
}  // namespace detail

/// Reads a variable by name; empty string = unset. The caller supplies the
/// platform lookup (MainComponent uses juce::SystemStats), so this stays pure.
using EnvLookup = std::function<std::string(const char* name)>;

/// Builds the settings from `RTA_API_ENABLED` (1 / true / on / yes,
/// case-insensitive; anything else, including unset, is OFF), `RTA_API_PORT`
/// (1..65535; anything else keeps the default) and `RTA_API_TOKEN`.
/// Silent fallbacks, by design (no UI exists at this point of start-up): an
/// unparseable `RTA_API_PORT` keeps 4736, and any ENABLED spelling other than
/// the four above -- including a value with a trailing space -- leaves the API
/// OFF. `apiStatusText` / the window title report the outcome that matters.
/// The bind address is deliberately NOT configurable here: it stays the
/// loopback default, so enabling the API never exposes it to the network.
[[nodiscard]] inline ApiSettings apiSettingsFromEnvironment(const EnvLookup& lookup) {
    ApiSettings settings;

    std::string enabled = lookup("RTA_API_ENABLED");
    std::transform(enabled.begin(), enabled.end(), enabled.begin(),
                   detail::lowerAscii);
    settings.enabled = enabled == "1" || enabled == "true" || enabled == "on" || enabled == "yes";

    const std::string port = lookup("RTA_API_PORT");
    if (!port.empty() && port.size() <= 5 &&
        std::all_of(port.begin(), port.end(), detail::isAsciiDigit)) {
        const int value = std::stoi(port);
        if (value >= 1 && value <= 65535) {
            settings.port = value;
        }
    }

    settings.token = lookup("RTA_API_TOKEN");
    return settings;
}

}  // namespace rta::api
