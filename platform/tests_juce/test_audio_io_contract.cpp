// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_platform_juce_tests.
//
// M1 (device-type/name read-back), M5 (a device that dies, nothing that
// resurrects it) and M6 (callbacks that outlive the device) from
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §5.8, automated wherever the
// decision-record rule under test is pure logic on AudioIo rather than a
// JUCE-backend behaviour. See platform/tests_juce/CMakeLists.txt for why no
// juce::AudioIODevice is ever opened here except by AudioIo::start() in the
// M1 tests, per the plan's own instruction.
#include <catch2/catch_test_macros.hpp>

#include "FakeAudioIODevice.h"
#include "rta/platform/AudioIo.h"

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

using rta::platform::AudioIo;
using rta::platform::ChannelRole;
using rta::platform::Fault;
using rta::platform::test::FakeAudioIODevice;

namespace {
constexpr const char* kBogusTypeName = "Definitely Not A Real Device Type";
constexpr const char* kBogusDeviceName = "Definitely Not A Real Device";
}  // namespace

// --- M1: read the device type/name BACK from the manager --------------------
//
// The plan's own trap table (docs/plans/2026-08-27-audioio-rta-impl-plan.md,
// the row citing "Read the device type BACK from the manager; never echo the
// requested string") states the rule this proves: JUCE silently keeps the
// current device type if the requested one is not registered, so a caller
// that echoes the request instead of reading currentState() shows a lie in
// the UI. This is the honest automatable core of M1: it proves the read-back
// half unconditionally, on any machine, with any device state. It does NOT
// prove JUCE's silent-fallback behaviour itself -- that a bogus type really
// is ignored rather than throwing, hanging, or crashing -- which is exactly
// what the human pass at M1 (docs/plans/.../§5.8) exists to observe against
// real hardware.

TEST_CASE("desired device type is never echoed back before any start attempt",
          "[audio_io][m1]") {
    AudioIo io;
    io.setDesiredDeviceType(kBogusTypeName);
    CHECK(io.currentState().typeName != kBogusTypeName);
}

TEST_CASE(
    "desired device type is never echoed back after a start attempt, "
    "whether or not that attempt opened a device",
    "[audio_io][m1]") {
    AudioIo io;
    io.setDesiredDeviceType(kBogusTypeName);
    // Return value deliberately ignored: the invariant under test holds
    // either way (a CI box may have no device at all).
    io.start();
    CHECK(io.currentState().typeName != kBogusTypeName);
    io.stop();
}

TEST_CASE("desired device name is never echoed back before any start attempt",
          "[audio_io][m1]") {
    AudioIo io;
    io.setDesiredDevice(kBogusDeviceName);
    CHECK(io.currentState().deviceName != kBogusDeviceName);
}

TEST_CASE(
    "desired device name is never echoed back after a start attempt, "
    "whether or not that attempt opened a device",
    "[audio_io][m1]") {
    AudioIo io;
    io.setDesiredDevice(kBogusDeviceName);
    io.start();
    CHECK(io.currentState().deviceName != kBogusDeviceName);
    io.stop();
}

// --- M5: a device that dies, and nothing that resurrects it ------------------
//
// "Unplug the interface" is, at the code level, one call to
// audioDeviceError() -- no device is opened to test this. audioDeviceStopped()
// stands in for "the device has stopped" wherever this file needs the bus to
// have been active beforehand, without ever calling start().

TEST_CASE("a device error records the fault and clears isRunning()",
          "[audio_io][m5]") {
    AudioIo io;
    FakeAudioIODevice fake(48000.0, 256, 2, 2);

    io.audioDeviceAboutToStart(&fake);
    REQUIRE(io.bus().config().setRole(0, ChannelRole::Measurement));
    REQUIRE(io.bus().isActive());

    const auto sequenceBefore = io.lastFault().sequence;

    io.audioDeviceError("simulated cable pull");

    const Fault fault = io.lastFault();
    CHECK(fault.kind == Fault::Kind::DeviceError);
    CHECK(fault.message == "simulated cable pull");
    CHECK(fault.sequence == sequenceBefore + 1);
    CHECK_FALSE(io.isRunning());

    // NOT proven here: isRunning() transitioning from true to false.
    // running_ is only ever set true inside AudioIo::start() (see
    // platform/src/AudioIo_Devices.cpp), which opens a real device -- so
    // this test, which never calls start(), can only show isRunning() is
    // false AFTER the error, not that it flipped from true. Proving the
    // transition would require either opening a real device (M2/M7
    // territory) or a production change to inject the "running" state,
    // neither of which this task allows.

    // This is the one assertion in this file that FAILS against the current
    // implementation: AudioIo::audioDeviceError() (platform/src/AudioIo.cpp)
    // records the fault and clears running_, but never calls
    // bus_.setActive(false). The plan's own M5 row says the postcondition is
    // "the bus stays inactive" -- see the report for why this is left as a
    // finding, not "fixed" by touching platform/src/.
    CHECK_FALSE(io.bus().isActive());
}

TEST_CASE(
    "stop() after a device error is safe and does not touch the fault record",
    "[audio_io][m5]") {
    AudioIo io;
    FakeAudioIODevice fake(48000.0, 256, 2, 2);
    io.audioDeviceAboutToStart(&fake);

    io.audioDeviceError("simulated cable pull");
    const auto faultBefore = io.lastFault();

    // running_ was never set true (start() was never called), so stop()'s
    // exchange-and-check guard treats this exactly like a second stop() on
    // an already-stopped object: it must return immediately, not crash, and
    // not touch the fault record.
    io.stop();

    const auto faultAfter = io.lastFault();
    CHECK(faultAfter.kind == faultBefore.kind);
    CHECK(faultAfter.message == faultBefore.message);
    CHECK(faultAfter.sequence == faultBefore.sequence);
}

// --- M6: callbacks that outlive the device -----------------------------------
//
// Some platform/JUCE combinations keep firing the audio callback after
// closeAudioDevice() (see AudioIo.h's comment on audioDeviceStopped()).
// audioDeviceStopped() is what actually marks the bus inactive on this path
// (unlike audioDeviceError() above) -- called here directly. stop() is also
// exercised first, on an object that was never started, purely for its
// no-crash guarantee.

TEST_CASE(
    "a callback firing after the device stops is absorbed: outputs cleared, "
    "nothing written, no drop counted",
    "[audio_io][m6]") {
    AudioIo io;
    FakeAudioIODevice fake(48000.0, 256, 2, 2);

    io.audioDeviceAboutToStart(&fake);
    REQUIRE(io.bus().config().setRole(0, ChannelRole::Measurement));
    REQUIRE(io.bus().isActive());

    io.stop();                // never started -- exercised for its no-crash guarantee
    io.audioDeviceStopped();  // what actually deactivates the bus on this path
    REQUIRE_FALSE(io.bus().isActive());

    const auto dropsBefore = io.bus().totalDrops();

    std::vector<float> in0(256, 5.0f), in1(256, 5.0f);
    std::vector<float> out0(256, 42.0f), out1(256, 42.0f);
    const float* inputs[2] = {in0.data(), in1.data()};
    float* outputs[2] = {out0.data(), out1.data()};

    const juce::AudioIODeviceCallbackContext context;
    io.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 256, context);

    CHECK(std::all_of(out0.begin(), out0.end(), [](float v) { return v == 0.0f; }));
    CHECK(std::all_of(out1.begin(), out1.end(), [](float v) { return v == 0.0f; }));
    REQUIRE(io.bus().ring(0) != nullptr);
    CHECK(io.bus().ring(0)->availableToRead() == 0);
    CHECK(io.bus().totalDrops() == dropsBefore);
}

TEST_CASE(
    "a flood of post-stop callbacks racing audioDeviceStopped() does not "
    "crash and leaves the bus inactive",
    "[audio_io][m6][threads]") {
    AudioIo io;
    FakeAudioIODevice fake(48000.0, 256, 2, 2);
    io.audioDeviceAboutToStart(&fake);
    REQUIRE(io.bus().config().setRole(0, ChannelRole::Measurement));

    std::thread stopper([&io] {
        for (int i = 0; i < 200; ++i) {
            io.audioDeviceStopped();
            std::this_thread::yield();
        }
    });

    std::vector<float> in0(256, 1.0f), in1(256, 1.0f);
    const float* inputs[2] = {in0.data(), in1.data()};
    const juce::AudioIODeviceCallbackContext context;
    for (int i = 0; i < 20000; ++i) {
        std::vector<float> out0(256, 7.0f), out1(256, 7.0f);
        float* outputs[2] = {out0.data(), out1.data()};
        io.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 256, context);
    }

    stopper.join();
    io.audioDeviceStopped();
    CHECK_FALSE(io.bus().isActive());

    SUCCEED("no crash across 20000 racing callbacks");
}
