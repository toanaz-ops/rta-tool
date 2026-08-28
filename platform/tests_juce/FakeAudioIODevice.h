// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_platform_juce_tests.
//
// A test-only juce::AudioIODevice that never touches a driver. It exists so
// AudioIo's device-thread overrides (audioDeviceAboutToStart, in particular)
// can be called directly in a test with a real juce::AudioIODevice* argument,
// with no hardware and no juce::AudioDeviceManager involved at all.
//
// AudioIo.cpp only ever reads two things off the device it is handed in
// audioDeviceAboutToStart(): getCurrentSampleRate() and
// getActiveInputChannels().countNumberOfSetBits(). Those two are answered
// honestly, from whatever this fake was constructed with. Every other pure
// virtual on juce::AudioIODevice is implemented minimally so the class is
// concrete -- open()/start() just flip a local flag, nothing they return is
// read by any test in this directory.
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

namespace rta::platform::test {

class FakeAudioIODevice final : public juce::AudioIODevice {
public:
    FakeAudioIODevice(double sampleRate, int bufferSize, int numInputChannels,
                       int numOutputChannels)
        : juce::AudioIODevice("FakeDevice", "FakeType"),
          sampleRate_(sampleRate),
          bufferSize_(bufferSize),
          numInputChannels_(numInputChannels),
          numOutputChannels_(numOutputChannels) {}

    // -- what AudioIo::audioDeviceAboutToStart actually reads ---------------
    double getCurrentSampleRate() override { return sampleRate_; }

    juce::BigInteger getActiveInputChannels() const override {
        return channelMask(numInputChannels_);
    }

    // -- answered honestly from construction args, never a driver -----------
    int getCurrentBufferSizeSamples() override { return bufferSize_; }

    juce::BigInteger getActiveOutputChannels() const override {
        return channelMask(numOutputChannels_);
    }

    juce::StringArray getOutputChannelNames() override {
        return channelNames(numOutputChannels_, "Out");
    }

    juce::StringArray getInputChannelNames() override {
        return channelNames(numInputChannels_, "In");
    }

    juce::Array<double> getAvailableSampleRates() override { return {sampleRate_}; }
    juce::Array<int> getAvailableBufferSizes() override { return {bufferSize_}; }
    int getDefaultBufferSize() override { return bufferSize_; }

    // -- minimal, honest, and never contacting a backend ---------------------
    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double,
                       int) override {
        opened_ = true;
        return {};  // empty = success, matching juce::AudioIODevice::open's contract
    }

    void close() override { opened_ = false; }
    bool isOpen() override { return opened_; }

    void start(juce::AudioIODeviceCallback* callback) override {
        callback_ = callback;
        playing_ = true;
    }

    void stop() override { playing_ = false; }
    bool isPlaying() override { return playing_; }
    juce::String getLastError() override { return {}; }

    int getCurrentBitDepth() override { return 32; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }

private:
    static juce::BigInteger channelMask(int count) {
        juce::BigInteger mask;
        mask.setRange(0, count, true);
        return mask;
    }

    static juce::StringArray channelNames(int count, const char* prefix) {
        juce::StringArray names;
        for (int i = 0; i < count; ++i) {
            names.add(juce::String(prefix) + " " + juce::String(i + 1));
        }
        return names;
    }

    double sampleRate_;
    int bufferSize_;
    int numInputChannels_;
    int numOutputChannels_;
    bool opened_ = false;
    bool playing_ = false;
    juce::AudioIODeviceCallback* callback_ = nullptr;
};

}  // namespace rta::platform::test
