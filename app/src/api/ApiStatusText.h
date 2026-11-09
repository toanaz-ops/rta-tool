// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/api. No JUCE, no Qt, no audio-device API.
// H8 review M-2: `RTA_API_ENABLED=1` with a port that will not bind used to
// do nothing visible -- `ApiServer` records `running() == false` and the
// composition root never read it, so an operator who asked for the API got
// silence. This is the one sentence the operator is told instead.
#pragma once

#include "api/ApiSettings.h"

#include <optional>
#include <string>

namespace rta::api {

/// Empty when there is nothing to report: the API was not asked for, or it is
/// running. Otherwise one line naming the port and, when the policy refused
/// the configuration, the reason.
[[nodiscard]] inline std::string apiNotRunningText(const ApiSettings& settings, bool running,
                                                   const std::optional<std::string>& refusal) {
    if (!settings.enabled || running) {
        return {};
    }
    std::string text = "API ENABLED BUT NOT RUNNING";
    if (refusal.has_value()) {
        text += ": " + *refusal;
    } else {
        text += ": could not bind " + settings.bindAddress + ":" + std::to_string(settings.port) +
                " (port in use?)";
    }
    return text;
}

}  // namespace rta::api
