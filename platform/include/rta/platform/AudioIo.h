// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_platform. Owns JUCE's audio device API; see
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.2, §3.3 and the decision
// record docs/specs/2026-08-27-platform-audioio-design.md.
#pragma once

// Module include rather than <JuceHeader.h>: JuceHeader.h is generated only
// for targets created with a juce_add_* function, and rta_platform is a
// plain add_library so this header can compile there and inside the
// analysis-test target's include path without one.
#include <juce_audio_devices/juce_audio_devices.h>

#include "rta/platform/CaptureBus.h"
#include "rta/platform/DeviceState.h"
#include "rta/platform/Fault.h"
#include "rta/platform/OutputEngine.h"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace rta::platform {

/// Implements `juce::AudioIODeviceCallback` directly (decision record:
/// "JUCE-native callback"), the shape proven in production by handsfree's
/// `AudioEngine`. `AudioIo` owns the `AudioDeviceManager`, the device
/// enumeration surface, and the fault record; `CaptureBus` (rta_platform_types,
/// no JUCE) owns the rings, the role table, the drop counters and the
/// validity atomic that the callback actually touches.
///
/// ## Threading model
///
///  - `start()` / `stop()` / the setters and getters below: message thread
///    only. None of them are lock-free and none may be called from the
///    audio callback.
///  - `audioDeviceIOCallbackWithContext()`: the real-time audio thread. It
///    does exactly three things -- `ScopedNoDenormals`, one call into
///    `bus_.pushFromCallback()`, one call into `output_.render()` (L7-OUT:
///    renders the active generator source into the routed outputs and clears
///    every other channel it was handed -- the single call that replaced the
///    old unconditional clear loop) -- and nothing else lives here on
///    purpose (plan §1.2). No allocation, no locks.
///  - `audioDeviceAboutToStart()` / `audioDeviceStopped()`: the device
///    thread, called by JUCE outside the audio callback's own dispatch (see
///    the comment on `audioDeviceAboutToStart` below for why that ordering
///    is what makes `CaptureBus::prepare` safe to call from here with no
///    extra synchronisation).
///  - `audioDeviceError()`: the device thread. Writes `fault_` under
///    `faultLock_`; the audio callback never touches it (trap T-8:
///    `juce::String` is reference-counted and cannot cross threads, so the
///    fault is stored as `std::string`, matching `Fault`'s own contract).
class AudioIo final : public juce::AudioIODeviceCallback {
public:
    /// `bus_` is constructed at the fixed `kFixedRingCapacitySamples` (see
    /// `CaptureBus.h`) -- capacity is no longer a per-open parameter derived
    /// from the buffer size (that logic used to live here as
    /// `ringCapacityFor` and has been removed along with the reallocating
    /// `CaptureBus::prepare` it fed; see `CaptureBus.h`'s class comment for
    /// why prepare() can no longer take a capacity at all).
    AudioIo() : bus_(kFixedRingCapacitySamples) {}
    ~AudioIo() override { stop(); }

    /// Opens the device with the desired type/name (applied by the setters
    /// below), registers this as the callback, and returns whether it
    /// succeeded. On failure the reason is available from `lastFault()`.
    bool start();

    /// Idempotent: a second call, or a call when never started, is a no-op.
    /// `removeAudioCallback` blocks until the audio thread has released the
    /// callback, so this must never be called FROM the audio thread itself.
    void stop();

    [[nodiscard]] bool isRunning() const noexcept;

    /// Applied on the NEXT `start()`, not immediately -- matching handsfree's
    /// `AudioEngine::setAudioDeviceType` / `setAudioDevice`.
    void setDesiredDeviceType(std::string typeName);
    void setDesiredDevice(std::string deviceName);

    /// @return false if refused: no device open, a non-positive value, or a
    /// rate/size the hardware does not accept. On success the device
    /// restarts, so `audioDeviceAboutToStart` re-reads the rate/buffer size
    /// and retargets the bus (decision record: "buffer-size/rate changes
    /// need a reload path outside the callback").
    bool setSampleRate(double newRate);
    bool setBufferSize(int newSize);

    // Device enumeration. Non-const where JUCE forces it
    // (`getAvailableDeviceTypes()`, `AudioIODevice::getAvailableSampleRates()`
    // are non-const in JUCE, and listing device names additionally requires
    // `scanForDevices()` first, which mutates the type object) -- exactly
    // what handsfree's `AudioEngine` documents at the same call sites.
    [[nodiscard]] std::vector<std::string> availableDeviceTypeNames();
    [[nodiscard]] std::vector<std::string> availableDeviceNames();
    [[nodiscard]] std::vector<double> availableSampleRates();
    [[nodiscard]] std::vector<int> availableBufferSizes();

    /// Read BACK from the manager/device -- never the requested string.
    /// JUCE silently keeps the current device type if the requested one
    /// (e.g. "ASIO" with no driver installed) is not registered; a caller
    /// that echoed the request would show the user a lie (decision record).
    [[nodiscard]] DeviceState currentState() const;

    /// The last fault recorded, or `Fault::Kind::None` if the device has
    /// never failed. Mutex-guarded (see the class comment); `sequence`
    /// increments on every new fault so a poller can distinguish "still the
    /// same problem" from "a new one just landed" without string compares.
    [[nodiscard]] Fault lastFault() const;

    /// The analysis side reads from here. Never null, never reseated.
    [[nodiscard]] CaptureBus& bus() noexcept { return bus_; }
    [[nodiscard]] const CaptureBus& bus() const noexcept { return bus_; }

    /// L7-OUT (record docs/dsp/2026-09-06-l7-output-path.md sec.6): the
    /// generator output path every solver and G20 auto solo/mute drive
    /// through `setSource`/`routeOutput`/`armSource`. Never null, never
    /// reseated -- same contract as `bus()`.
    [[nodiscard]] OutputEngine& output() noexcept { return output_; }
    [[nodiscard]] const OutputEngine& output() const noexcept { return output_; }

    // juce::AudioIODeviceCallback -------------------------------------------

    /// THE hard-real-time boundary. Body: `ScopedNoDenormals` first, one
    /// call into `bus_.pushFromCallback`, one call into `output_.render`.
    /// Everything the decision record warns about (role bounds, short-write
    /// counting, the validity check) lives inside `pushFromCallback`, not
    /// here -- see `rta::platform::CaptureBus`. Everything the L7-OUT record
    /// warns about (no alloc/lock/IO/FFT, JUCE's write-or-clear contract on
    /// every output channel) lives inside `OutputEngine::render`, not here --
    /// see `rta::platform::OutputEngine`. Parameter names kept even where
    /// unused past the pass-through, for readability at the call site; none
    /// of them can be omitted without also omitting ones that ARE used.
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

    /// Retargets `bus_` to the device's actual rate/channel count and drains
    /// every ring (decision record: "retargets rate-dependent state AND
    /// drains every ring" -- stale samples from the previous session would
    /// otherwise splice onto the new one and mislabel every bin-to-Hz
    /// conversion). Also retargets `output_` (L7-OUT record sec.4-5): bumps
    /// its epoch, disarms the active source, and rescales every gate's ramp
    /// length to the new rate -- same "safe from here, no extra
    /// synchronisation" reasoning as `bus_.prepare` below. Safe to call
    /// `CaptureBus::prepare` here with no extra synchronisation: JUCE calls
    /// this BEFORE inserting the callback into its dispatch list, so the
    /// audio thread cannot be inside `pushFromCallback` yet.
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;

    /// Marks the bus inactive BEFORE the device is torn down further, so a
    /// callback that keeps firing after `closeAudioDevice()` on some
    /// platform/JUCE combinations (decision record) sees an inactive bus and
    /// writes nothing rather than touching rings mid-teardown.
    void audioDeviceStopped() override;

    /// Records the fault and clears `running_`. No retry, no reconnect
    /// timer here (decision record: "no auto-reconnect in the I/O layer;
    /// report the fault, let the app own retry") -- that policy belongs to
    /// whatever in app/ owns the device panel, not to this class.
    void audioDeviceError(const juce::String& errorMessage) override;

private:
    void recordFault(Fault::Kind kind, const std::string& message);

    juce::AudioDeviceManager deviceManager_;
    CaptureBus bus_;
    OutputEngine output_;

    std::atomic<bool> running_{false};

    juce::String desiredDeviceType_;
    juce::String desiredDeviceName_;

    // std::string behind a mutex, not juce::String -- trap T-8. Written on
    // the device thread (audioDeviceError, and start()'s open-failure path),
    // read on the message thread by lastFault(). The audio callback never
    // touches it.
    mutable std::mutex faultLock_;
    Fault fault_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioIo)
};

}  // namespace rta::platform
