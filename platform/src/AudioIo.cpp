// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_platform.
//
// Lifecycle (start/stop) and the three AudioIODeviceCallback overrides that
// are NOT the hot callback body. Device enumeration/configuration is split
// into AudioIo_Devices.cpp purely to keep both files under the 400-line cap
// (plan §3.3) -- both define methods of the same class.
#include "rta/platform/AudioIo.h"

namespace rta::platform {

namespace {

// Requested channel counts for AudioDeviceManager::initialise*(). Input asks
// for the full role-table width (kMaxChannels, covering a MADI or Dante
// interface, per plan §1.4); JUCE opens as many as the device actually has,
// never more, so this is a ceiling, not a promise. Output asks for a modest
// stereo pair purely so devices that require at least one active output
// channel to open at all still succeed -- this deliverable never writes
// anything but silence to it (the callback body below), and driving a
// generator OUT to a device is explicitly out of scope (plan §0).
constexpr int kRequestedInputChannels = kMaxChannels;
constexpr int kRequestedOutputChannels = 2;

}  // namespace

bool AudioIo::start() {
    if (running_.load(std::memory_order_acquire)) {
        return true;
    }

    // Switch device type first if the desired one differs from what is
    // currently selected. JUCE opens a device of the new type synchronously,
    // or silently keeps the current one if the requested type is not
    // registered (decision record) -- currentState() is how a caller finds
    // out which one actually happened.
    if (desiredDeviceType_.isNotEmpty() &&
        deviceManager_.getCurrentAudioDeviceType() != desiredDeviceType_) {
        deviceManager_.setCurrentAudioDeviceType(desiredDeviceType_, true);
    }

    const juce::String error =
        desiredDeviceName_.isEmpty()
            ? deviceManager_.initialiseWithDefaultDevices(kRequestedInputChannels,
                                                            kRequestedOutputChannels)
            : deviceManager_.initialise(kRequestedInputChannels, kRequestedOutputChannels,
                                         nullptr, true, desiredDeviceName_, nullptr);

    if (error.isNotEmpty()) {
        recordFault(Fault::Kind::OpenFailed, error.toStdString());
        return false;
    }

    // Whatever went wrong last time no longer applies.
    recordFault(Fault::Kind::None, {});

    // If the device is already running, audioDeviceAboutToStart() fires
    // before this call returns.
    deviceManager_.addAudioCallback(this);
    running_.store(true, std::memory_order_release);
    return true;
}

void AudioIo::stop() {
    // Exchange-and-check: tear down exactly once even if stop() is called
    // twice, or the device already died under us (audioDeviceError() also
    // clears running_ from the device thread).
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    // No auto-reconnect lives here (decision record) -- this is the FULL
    // extent of what stop() does: mark the bus inactive, release the
    // callback, close the device. Nothing retries.
    bus_.setActive(false);

    // removeAudioCallback() blocks until the audio thread has released the
    // callback, so it must never be called from the audio thread itself.
    deviceManager_.removeAudioCallback(this);
    deviceManager_.closeAudioDevice();
}

bool AudioIo::isRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
}

void AudioIo::setDesiredDeviceType(std::string typeName) {
    desiredDeviceType_ = juce::String(typeName);
}

void AudioIo::setDesiredDevice(std::string deviceName) {
    desiredDeviceName_ = juce::String(deviceName);
}

void AudioIo::recordFault(Fault::Kind kind, const std::string& message) {
    const std::lock_guard<std::mutex> lock(faultLock_);
    fault_.kind = kind;
    fault_.message = message;
    ++fault_.sequence;
}

Fault AudioIo::lastFault() const {
    const std::lock_guard<std::mutex> lock(faultLock_);
    return fault_;
}

//==============================================================================
// juce::AudioIODeviceCallback -- the real-time audio thread and its two
// device-thread neighbours.

void AudioIo::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData, int numInputChannels,
    float* const* outputChannelData, int numOutputChannels, int numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/) {
    // Decision record, row 1 / plan §9.1 trap: FTZ/DAZ must be the FIRST
    // statement in this function, before anything else touches a float.
    // Without it, filter/accumulator state that decays toward digital
    // silence (an engineer muting a channel) can settle into the subnormal
    // range and never reach exact zero; subnormal SSE arithmetic traps to
    // microcode. Measured in production (handsfree's AudioEngine, same
    // callback shape): ~80x CPU with denormals enabled versus flushed
    // (77 ms/s vs 0.9 ms/s of audio). `check_callback_shape.cmake`
    // (platform/tests/) greps this exact function for the violation.
    const juce::ScopedNoDenormals noDenormals;

    // Plan §1.2: the callback is reduced to this one call. Every rule the
    // decision record states about the callback body -- bounds against the
    // channel count THIS block actually received, count rather than
    // swallow short writes, do nothing when the bus is inactive -- lives
    // inside pushFromCallback (rta_platform_types::CaptureBus), which takes
    // plain C arrays and is therefore provable with no device at all.
    bus_.pushFromCallback(inputChannelData, numInputChannels, numSamples);

    // JUCE requires every output channel to be written or cleared -- the
    // buffers are NOT pre-cleared for us. This deliverable produces no audio
    // output (plan §0: "generator output to a device" is out of scope), so
    // clearing is the entire output-side contract.
    if (outputChannelData != nullptr) {
        for (int ch = 0; ch < numOutputChannels; ++ch) {
            if (outputChannelData[ch] != nullptr) {
                juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples);
            }
        }
    }
}

void AudioIo::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    if (device == nullptr) {
        return;
    }

    const double sampleRate = device->getCurrentSampleRate();

    // The channel count to prepare rings for: what the device reports active
    // on the input side, clamped to kMaxChannels. This is DIFFERENT from the
    // "channel count the callback actually received" bounds rule -- that one
    // (decision record) is enforced per-block inside pushFromCallback against
    // whatever numInputChannels arrives with THAT call, which can be lower
    // than this if the device delivers fewer channels than advertised. This
    // number only decides how many rings exist to be written into.
    const int activeInputs = device->getActiveInputChannels().countNumberOfSetBits();
    const int numChannels = activeInputs > kMaxChannels ? kMaxChannels : activeInputs;

    // Decision record: "audioDeviceAboutToStart retargets rate-dependent
    // state AND drains every ring". CaptureBus::prepare() does both in one
    // call -- resets every ring (back to empty; the drain) and bumps the
    // epoch so the analysis thread notices and rebuilds its own state. No
    // capacity argument any more: every ring was sized once, in bus_'s
    // constructor, at the fixed kFixedRingCapacitySamples (see CaptureBus.h)
    // -- that is what makes it safe for prepare() to run here, on the
    // device thread, while AnalysisThread may be mid-drain on its own
    // thread with no lock between the two (see CaptureBus.h's class
    // comment for the hazard this closes). Safe to call here with no extra
    // synchronisation for the SAME reason it always was for the write side:
    // JUCE invokes this BEFORE inserting the callback into its dispatch
    // list (see the header), so the audio thread cannot be inside
    // pushFromCallback yet.
    bus_.prepare(sampleRate, numChannels);

    // Only NOW does the callback start doing anything: setActive(true) after
    // prepare() has finished, never before.
    bus_.setActive(true);
}

void AudioIo::audioDeviceStopped() {
    // Decision record: "the callback defensively checks a validity atomic" --
    // on some platform/JUCE combinations the callback keeps firing after
    // closeAudioDevice(). Marking the bus inactive HERE, on the device
    // thread, before stop() proceeds to remove the callback and close the
    // device, is what makes pushFromCallback's isActive() check meaningful
    // for that window rather than a check against state that is always true
    // until it is too late.
    bus_.setActive(false);
}

void AudioIo::audioDeviceError(const juce::String& errorMessage) {
    running_.store(false, std::memory_order_release);

    // A device error is the THIRD way the device dies, alongside stop() and
    // audioDeviceStopped() -- both of those already mark the bus inactive
    // (see audioDeviceStopped()'s comment above for why that matters), but
    // this path previously left it armed. Set it here, before recordFault()
    // takes the mutex, so the window in which a stray callback could still
    // be accepted is as short as possible.
    bus_.setActive(false);

    // RETAIN the message rather than only logging it -- without this the
    // device panel goes dark with the reason available nowhere in the UI.
    // Runs on the device thread, never the audio callback (trap T-8).
    recordFault(Fault::Kind::DeviceError, errorMessage.toStdString());

    juce::Logger::writeToLog("AudioIo device error: " + errorMessage);
}

}  // namespace rta::platform
