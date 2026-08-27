// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_platform_types. No JUCE, no Qt, no audio-device API.
#pragma once

#include <cstdint>
#include <string>

namespace rta::platform {

/// The last thing that went wrong with the audio device, if anything. Owned
/// by `AudioIo` behind a mutex (never touched by the audio callback itself),
/// and read by the message thread to drive the device panel's fault line.
///
/// `sequence` increments every time a new fault is recorded, so a poller can
/// tell "still the same fault" from "a new one just landed" without string
/// comparison.
struct Fault {
    enum class Kind {
        None,           ///< Nothing wrong.
        OpenFailed,     ///< The device failed to open.
        DeviceError,    ///< JUCE reported an error while running.
        DeviceStopped,  ///< The device stopped unexpectedly (e.g. unplugged).
    };

    Kind kind = Kind::None;
    std::string message;
    std::uint32_t sequence = 0;
};

}  // namespace rta::platform
