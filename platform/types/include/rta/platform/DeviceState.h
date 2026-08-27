// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_platform_types. No JUCE, no Qt, no audio-device API.
#pragma once

#include <string>
#include <vector>

namespace rta::platform {

/// A snapshot of what the audio device is actually doing, read BACK from the
/// device manager rather than echoing whatever was requested (decision
/// record: JUCE silently keeps the current device type if the requested one
/// isn't available, so a caller that trusts the request instead of this
/// struct shows a lie in the UI).
///
/// Plain `std::string`, not `juce::String`: this is the app-facing side of
/// the platform layer, and `juce::String` is reference-counted and unsafe to
/// hand across the thread boundary this struct crosses.
struct DeviceState {
    std::string typeName;
    std::string deviceName;
    double sampleRate = 0.0;
    int bufferSize = 0;
    int numInputChannels = 0;
    int numOutputChannels = 0;
    bool open = false;
    double cpuLoad = 0.0;
    std::vector<std::string> inputChannelNames;
};

}  // namespace rta::platform
