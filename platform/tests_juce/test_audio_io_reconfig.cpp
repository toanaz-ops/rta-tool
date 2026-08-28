// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_platform_juce_tests.
//
// M3 (sample-rate change must not splice stale audio) and M4 (buffer-size
// change must not deadlock) from
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §5.8. Both drive
// AudioIo::audioDeviceAboutToStart directly through FakeAudioIODevice --
// this is JUCE's own real call site for "the device is about to (re)start",
// invoked here with no device ever opened.
//
// Read CaptureBus::prepare's contract first (see
// platform/types/include/rta/platform/CaptureBus.h): it drains every ring,
// updates sampleRate(), and unconditionally bumps epoch() -- but it does NOT
// touch ChannelConfig (roles survive a prepare()). The assertions below are
// written against exactly that contract, not against anything stronger.
#include <catch2/catch_test_macros.hpp>

#include "FakeAudioIODevice.h"
#include "rta/platform/AudioIo.h"

#include <numeric>
#include <vector>

using rta::platform::AudioIo;
using rta::platform::ChannelRole;
using rta::platform::test::FakeAudioIODevice;

namespace {

/// Pushes a numChannels x numSamples block of samples counting up from
/// `base`, through the real callback entry point (not CaptureBus directly),
/// so this exercises AudioIo::audioDeviceIOCallbackWithContext too.
void pushRecognisableBlock(AudioIo& io, int numChannels, int numSamples, float base) {
    std::vector<std::vector<float>> in(static_cast<std::size_t>(numChannels),
                                        std::vector<float>(static_cast<std::size_t>(numSamples)));
    for (auto& ch : in) std::iota(ch.begin(), ch.end(), base);
    std::vector<const float*> inPointers;
    for (auto& ch : in) inPointers.push_back(ch.data());

    // The real callback also wants an output side to clear -- give it one so
    // this call matches the production callback shape exactly rather than a
    // shortcut into CaptureBus::pushFromCallback alone.
    std::vector<std::vector<float>> out(static_cast<std::size_t>(numChannels),
                                         std::vector<float>(static_cast<std::size_t>(numSamples)));
    std::vector<float*> outPointers;
    for (auto& ch : out) outPointers.push_back(ch.data());

    const juce::AudioIODeviceCallbackContext context;
    io.audioDeviceIOCallbackWithContext(inPointers.data(), numChannels, outPointers.data(),
                                         numChannels, numSamples, context);
}

}  // namespace

// --- M3: a sample-rate change must not splice stale audio -------------------
//
// The plan's own warning: a burst of impossible high-frequency energy after a
// rate change is exactly what stale rings splicing across the change
// produce. Proven here by pushing recognisable 48 kHz-session data, forcing
// the rate change, and checking that only fresh 96 kHz-session data comes
// back out.

TEST_CASE(
    "a sample-rate change drains every ring and advances the epoch, so no "
    "pre-change sample survives",
    "[audio_io][m3]") {
    AudioIo io;
    FakeAudioIODevice fake48k(48000.0, 512, 2, 2);

    io.audioDeviceAboutToStart(&fake48k);
    REQUIRE(io.bus().config().setRole(0, ChannelRole::Measurement));
    REQUIRE(io.bus().sampleRate() == 48000.0);

    pushRecognisableBlock(io, 2, 512, 1.0f);
    REQUIRE(io.bus().ring(0)->availableToRead() == 512);

    const auto epochBefore = io.bus().epoch();
    auto* ringBefore = io.bus().ring(0);

    FakeAudioIODevice fake96k(96000.0, 512, 2, 2);
    io.audioDeviceAboutToStart(&fake96k);

    // Identity, not just emptiness: prepare() resets in place (CaptureBus.h's
    // class comment) rather than reallocating, so a consumer holding this
    // pointer across the change stays valid.
    CHECK(io.bus().ring(0) == ringBefore);
    CHECK(io.bus().ring(0)->availableToRead() == 0);
    CHECK(io.bus().sampleRate() == 96000.0);
    CHECK(io.bus().epoch() > epochBefore);

    // Fresh, recognisable 96 kHz-session data comes back exactly as pushed --
    // not 512 old samples with new ones spliced on.
    pushRecognisableBlock(io, 2, 300, 9.0f);
    REQUIRE(io.bus().ring(0)->availableToRead() == 300);
    std::vector<float> out(300, -1.0f);
    REQUIRE(io.bus().ring(0)->read(out) == 300);
    CHECK(out.front() == 9.0f);
    CHECK(out.back() == 9.0f + 299.0f);
}

// --- M4: a buffer-size change must not deadlock ------------------------------
//
// Same rate and channel count; only the buffer size differs. The historical
// ASIO freeze came from reset logic living inside a callback that had
// already stopped firing -- a test that simply completes, with state left
// consistent afterwards, is meaningful evidence there is no such path left
// inside audioDeviceAboutToStart.

TEST_CASE(
    "a buffer-size change completes, drains every ring, and leaves state "
    "consistent",
    "[audio_io][m4]") {
    AudioIo io;
    FakeAudioIODevice fakeSmallBuffer(48000.0, 256, 2, 2);

    io.audioDeviceAboutToStart(&fakeSmallBuffer);
    REQUIRE(io.bus().config().setRole(0, ChannelRole::Measurement));

    pushRecognisableBlock(io, 2, 256, 2.0f);
    REQUIRE(io.bus().ring(0)->availableToRead() == 256);

    const auto epochBefore = io.bus().epoch();

    FakeAudioIODevice fakeLargeBuffer(48000.0, 1024, 2, 2);
    io.audioDeviceAboutToStart(&fakeLargeBuffer);  // completing at all is the M4 evidence

    CHECK(io.bus().ring(0)->availableToRead() == 0);
    CHECK(io.bus().sampleRate() == 48000.0);
    CHECK(io.bus().epoch() > epochBefore);
    CHECK(io.bus().numChannels() == 2);

    // Usable immediately afterwards -- no half-applied state.
    pushRecognisableBlock(io, 2, 128, 3.0f);
    CHECK(io.bus().ring(0)->availableToRead() == 128);
}
