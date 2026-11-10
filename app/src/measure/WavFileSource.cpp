// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/WavFileSource.h"

#include <limits>

namespace rta::measure {

std::unique_ptr<WavFileSource> WavFileSource::open(const juce::File& file, OpenFailure* why) {
    if (why != nullptr) *why = OpenFailure::NotReadable;
    if (!file.existsAsFile()) return nullptr;
    juce::AudioFormatManager formats;  // D1: no lossy codec is ever registered
    formats.registerFormat(new juce::WavAudioFormat(), true);
    formats.registerFormat(new juce::AiffAudioFormat(), false);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) return nullptr;
    if (reader->numChannels == 0 || reader->lengthInSamples <= 0) {
        if (why != nullptr) *why = OpenFailure::NoSamples;
        return nullptr;
    }
    return std::unique_ptr<WavFileSource>(new WavFileSource(std::move(reader)));
}

WavFileSource::WavFileSource(std::unique_ptr<juce::AudioFormatReader> reader)
    : reader_(std::move(reader)),
      channels_(static_cast<int>(reader_->numChannels)),
      sampleRate_(reader_->sampleRate),
      length_(reader_->lengthInSamples) {}

bool WavFileSource::read(int channel, std::int64_t startSample, std::span<float> out) const {
    if (channel < 0 || channel >= channels_ || startSample < 0 || out.empty() ||
        out.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    // A read past the end is padded with zeros by the reader, not refused: a short read
    // would be silently wrong data, so refuse it here (the contract is "never short").
    if (startSample + static_cast<std::int64_t>(out.size()) > length_) return false;
    // The reader fills only the channels whose pointer is non-null.
    destinations_.assign(static_cast<std::size_t>(channel) + 1, nullptr);
    destinations_[static_cast<std::size_t>(channel)] = out.data();
    return reader_->read(destinations_.data(), static_cast<int>(destinations_.size()), startSample,
                         static_cast<int>(out.size()));
}

}  // namespace rta::measure
