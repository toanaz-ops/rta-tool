// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. JUCE side of H11's offline transfer
// (docs/dsp/2026-11-10-g22-offline-wav-transfer.md D1, D2): the file reader
// behind W2's `SampleBlockSource`.
//
// Only WAV and AIFF are registered: a lossy codec alters phase and timing, which
// is exactly what a transfer function measures (D1). Each instance owns ONE
// `AudioFormatReader`, whose stream position moves on every read, so an
// instance belongs to a single thread: the analysis worker reads through its
// own; the message thread opens a separate short-lived one at LOAD time only to
// learn the channel count, rate and length.
#pragma once

#include "measure/WavTransfer.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace rta::measure {

class WavFileSource final : public SampleBlockSource {
public:
    /// Null when the file is missing, unreadable, not WAV/AIFF, or has no samples.
    [[nodiscard]] static std::unique_ptr<WavFileSource> open(const juce::File& file);

    [[nodiscard]] int numChannels() const override { return channels_; }
    [[nodiscard]] double sampleRate() const override { return sampleRate_; }
    [[nodiscard]] std::int64_t lengthSamples() const override { return length_; }
    [[nodiscard]] bool read(int channel, std::int64_t startSample, std::span<float> out) const override;

private:
    explicit WavFileSource(std::unique_ptr<juce::AudioFormatReader> reader);

    // The reader's stream position moves on a read; `read` is logically const.
    std::unique_ptr<juce::AudioFormatReader> reader_;
    int channels_ = 0;
    double sampleRate_ = 0.0;
    std::int64_t length_ = 0;
    mutable std::vector<float*> destinations_;
};

}  // namespace rta::measure
