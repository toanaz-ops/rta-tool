// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_platform.
//
// Device enumeration, sample-rate/buffer-size configuration, and the
// read-back status surface (currentState()). Split out of AudioIo.cpp purely
// to keep both files under the 400-line cap (plan §3.3) -- both define
// methods of the same rta::platform::AudioIo class.
#include "rta/platform/AudioIo.h"

namespace rta::platform {

std::vector<std::string> AudioIo::availableDeviceTypeNames() {
    std::vector<std::string> names;
    for (auto* type : deviceManager_.getAvailableDeviceTypes()) {
        if (type != nullptr) {
            names.push_back(type->getTypeName().toStdString());
        }
    }
    return names;
}

std::vector<std::string> AudioIo::availableDeviceNames() {
    auto* type = deviceManager_.getCurrentDeviceTypeObject();
    if (type == nullptr) {
        return {};
    }

    // JUCE requires scanForDevices() before getDeviceNames() -- without it
    // the list is whatever the last scan found, or empty on a fresh type
    // object. This is why the method cannot be const (see header).
    type->scanForDevices();

    std::vector<std::string> names;
    for (const auto& name : type->getDeviceNames()) {
        names.push_back(name.toStdString());
    }
    return names;
}

std::vector<double> AudioIo::availableSampleRates() {
    auto* device = deviceManager_.getCurrentAudioDevice();
    if (device == nullptr) {
        return {};
    }

    std::vector<double> rates;
    for (double rate : device->getAvailableSampleRates()) {
        rates.push_back(rate);
    }
    return rates;
}

std::vector<int> AudioIo::availableBufferSizes() {
    auto* device = deviceManager_.getCurrentAudioDevice();
    if (device == nullptr) {
        return {};
    }

    std::vector<int> sizes;
    for (int size : device->getAvailableBufferSizes()) {
        sizes.push_back(size);
    }
    return sizes;
}

bool AudioIo::setSampleRate(double newRate) {
    if (newRate <= 0.0 || deviceManager_.getCurrentAudioDevice() == nullptr) {
        return false;
    }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);

    if (setup.sampleRate == newRate) {
        return true;  // already there; not a failure
    }
    setup.sampleRate = newRate;

    // Decision record: setAudioDeviceSetup returns an EMPTY string on
    // success and the error text on failure -- the inverse of the usual
    // convention. `.isEmpty()` here reads backwards on purpose; do not
    // "fix" it to `.isNotEmpty()`. On success the device restarts, so
    // audioDeviceAboutToStart() re-reads the rate and retargets the bus.
    return deviceManager_.setAudioDeviceSetup(setup, true).isEmpty();
}

bool AudioIo::setBufferSize(int newSize) {
    if (newSize <= 0 || deviceManager_.getCurrentAudioDevice() == nullptr) {
        return false;
    }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);

    if (setup.bufferSize == newSize) {
        return true;
    }
    setup.bufferSize = newSize;

    // Same inverted convention as setSampleRate() above.
    return deviceManager_.setAudioDeviceSetup(setup, true).isEmpty();
}

DeviceState AudioIo::currentState() const {
    DeviceState state;

    // Read the type BACK from the manager -- never the requested string.
    // See the header and the decision record.
    state.typeName = deviceManager_.getCurrentAudioDeviceType().toStdString();

    auto* device = deviceManager_.getCurrentAudioDevice();
    if (device == nullptr) {
        return state;  // open = false, everything else at its default
    }

    state.deviceName = device->getName().toStdString();
    state.sampleRate = device->getCurrentSampleRate();
    state.bufferSize = device->getCurrentBufferSizeSamples();
    state.open = device->isOpen();
    state.cpuLoad = deviceManager_.getCpuUsage();

    for (const auto& name : device->getInputChannelNames()) {
        state.inputChannelNames.push_back(name.toStdString());
    }
    state.numInputChannels = static_cast<int>(state.inputChannelNames.size());
    state.numOutputChannels = device->getOutputChannelNames().size();

    return state;
}

}  // namespace rta::platform
