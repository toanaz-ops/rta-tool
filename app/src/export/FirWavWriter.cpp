// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. JUCE-using (WavAudioFormat) on
// purpose -- see FirWavExport.h's header comment for the split.
#include "export/FirWavExport.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>
#include <stdexcept>
#include <vector>

namespace rta::firexport {

void writeFirWav(const juce::File& file, const rta::dsp::FirResult& result,
                 Normalization normalization, WavSampleFormat format) {
    if (format != WavSampleFormat::Float32) {
        // record Sec.5: "Never an integer WAV" -- a boost puts |h[n]| > 1,
        // which float carries and int clips. There is no code path that
        // writes anything else; this is the refusal, not a branch to it.
        throw std::invalid_argument("writeFirWav: only 32-bit float is supported (record Sec.5)");
    }

    // Only .scale is needed here -- the WAV format carries no header field
    // for the trim; the text writer (FirTextWriter.cpp) reports that.
    const double scale = computeNormalizationScale(result.coefficientPeak, normalization).scale;

    std::vector<float> scaled(result.taps.size());
    for (std::size_t i = 0; i < result.taps.size(); ++i) {
        scaled[i] = static_cast<float>(static_cast<double>(result.taps[i]) * scale);
    }

    auto stream = std::make_unique<juce::FileOutputStream>(file);
    if (!stream->openedOk()) {
        throw std::invalid_argument("writeFirWav: cannot open " + file.getFullPathName().toStdString() +
                                    " for writing");
    }
    stream->setPosition(0);
    stream->truncate();

    juce::WavAudioFormat wavFormat;
    const auto options = juce::AudioFormatWriterOptions{}
                              .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint)
                              .withSampleRate(result.sampleRate)
                              .withNumChannels(1)
                              .withBitsPerSample(32);

    std::unique_ptr<juce::OutputStream> streamBase(stream.release());
    std::unique_ptr<juce::AudioFormatWriter> writer = wavFormat.createWriterFor(streamBase, options);
    if (writer == nullptr) {
        throw std::invalid_argument("writeFirWav: WavAudioFormat refused these write options");
    }

    const float* channels[1] = { scaled.data() };
    if (!writer->writeFromFloatArrays(channels, 1, static_cast<int>(scaled.size()))) {
        throw std::invalid_argument("writeFirWav: write failed");
    }
    // writer's destructor flushes and finalises the RIFF header.
}

}  // namespace rta::firexport
