// SPDX-License-Identifier: AGPL-3.0-or-later
// H11 W3 test support: writes a multichannel WAV or AIFF with JUCE's own writer, so a test (and
// tools/snapshot_wav.cpp) can hand `LOAD WAV` a real file. Lives in tests_juce, not app/tests:
// the app/tests/*.h glob is scanned by measure_has_no_framework_deps and this header is JUCE.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace wavfile {

enum class Container { Wav, Aiff };
enum class Sample { Pcm24, Pcm16, Float32 };

/// `channels[c][n]`, equal lengths. PCM clips outside +-1, so a caller scales first.
///
/// `timeReference` (WAV only), when set, is written into a Broadcast WAV `bext` chunk as its time
/// reference in samples. JUCE writes the chunk only for a non-zero value, so pass one.
inline bool write(const juce::File& file, Container container, Sample sample, double sampleRate,
                  const std::vector<std::vector<float>>& channels,
                  std::optional<std::int64_t> timeReference = std::nullopt) {
    if (channels.empty()) return false;
    file.deleteFile();
    auto stream = std::make_unique<juce::FileOutputStream>(file);
    if (!stream->openedOk()) return false;
    std::unique_ptr<juce::OutputStream> base(stream.release());

    const int bits = sample == Sample::Pcm24 ? 24 : sample == Sample::Pcm16 ? 16 : 32;
    auto options =
        juce::AudioFormatWriterOptions{}
            .withSampleFormat(sample == Sample::Float32 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                        : juce::AudioFormatWriterOptions::SampleFormat::integral)
            .withSampleRate(sampleRate)
            .withNumChannels(static_cast<int>(channels.size()))
            .withBitsPerSample(bits);
    if (timeReference.has_value() && container == Container::Wav) {
        options = options.withMetadata(juce::WavAudioFormat::bwavTimeReference, juce::String(*timeReference));
    }

    std::unique_ptr<juce::AudioFormatWriter> writer;
    if (container == Container::Wav) {
        juce::WavAudioFormat format;
        writer = format.createWriterFor(base, options);
    } else {
        juce::AiffAudioFormat format;
        writer = format.createWriterFor(base, options);
    }
    if (writer == nullptr) return false;

    std::vector<const float*> pointers;
    for (const auto& c : channels) pointers.push_back(c.data());
    const bool ok = writer->writeFromFloatArrays(pointers.data(), static_cast<int>(pointers.size()),
                                                 static_cast<int>(channels.front().size()));
    writer.reset();  // flushes and finalises the header
    return ok;
}

}  // namespace wavfile
