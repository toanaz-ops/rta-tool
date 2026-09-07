// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TDD sequence for the FIR WAV writer -- docs/plans/2026-09-07-L7-fir-impl-plan.md
// task F5. Written from docs/dsp/2026-09-06-l7-fir-export.md Sec.5, Sec.6.
// Headless: no window, no desktop peer, no message loop -- same convention
// app/tests_juce/CMakeLists.txt's other targets follow.

#include "export/FirWavExport.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <filesystem>
#include <memory>

using namespace rta::firexport;
using namespace rta::dsp;

namespace {

/// Removes itself so a failing assertion cannot leave the next run reading a
/// previous run's file -- same convention app/tests/test_session_store.cpp's
/// TempDir uses.
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char* name) : path(std::filesystem::temp_directory_path() / "rta-fir-wav-test" / name) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

FirResult makeResult() {
    FirResult r;
    r.taps = { 0.1f, 0.5f, 1.25f, 0.5f, 0.1f };   // > 1.0 on purpose: the case an int WAV clips
    r.sampleRate = 48000.0;
    r.phase = FirPhase::Linear;
    r.method = FirMethod::FrequencySampling;
    r.window = WindowType::Hann;
    r.groupDelaySamples = 2;
    r.peakGainDb = 1.9;
    r.coefficientPeak = 1.25f;
    r.designFftSize = 64;
    return r;
}

}  // namespace

TEST_CASE("Write then read back: sample rate, channel count, length, and samples all round-trip",
          "[fir_wav]") {
    // T1 (first).
    TempDir dir("roundtrip");
    const juce::File file(juce::String((dir.path / "test.wav").string()));

    const auto result = makeResult();
    writeFirWav(file, result, Normalization::AsDesigned);

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatReader> reader(
        wavFormat.createReaderFor(file.createInputStream().release(), true));
    REQUIRE(reader != nullptr);

    CHECK(reader->sampleRate == result.sampleRate);
    CHECK(reader->numChannels == 1u);
    CHECK(static_cast<std::size_t>(reader->lengthInSamples) == result.taps.size());

    juce::AudioBuffer<float> buffer(1, static_cast<int>(result.taps.size()));
    REQUIRE(reader->read(&buffer, 0, static_cast<int>(result.taps.size()), 0, true, true));

    for (std::size_t i = 0; i < result.taps.size(); ++i) {
        const double expected = static_cast<double>(result.taps[i]);
        const double actual = static_cast<double>(buffer.getSample(0, static_cast<int>(i)));
        const double residual = std::abs(actual - expected);
        CAPTURE(i, actual, expected, residual);
        CHECK(residual <= 1e-7);
    }
}

TEST_CASE("The file is 32-bit IEEE float, and an integer-format request is refused", "[fir_wav]") {
    // T2.
    TempDir dir("floatformat");
    const juce::File file(juce::String((dir.path / "test.wav").string()));

    writeFirWav(file, makeResult(), Normalization::AsDesigned);

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatReader> reader(
        wavFormat.createReaderFor(file.createInputStream().release(), true));
    REQUIRE(reader != nullptr);
    CHECK(reader->usesFloatingPointData == true);

    const juce::File refused(juce::String((dir.path / "refused.wav").string()));
    CHECK_THROWS_AS(
        writeFirWav(refused, makeResult(), Normalization::AsDesigned, WavSampleFormat::Int16),
        std::invalid_argument);
}

TEST_CASE("firFilenameStem carries sample rate, tap count, and phase", "[fir_wav]") {
    // T3.
    CHECK(firFilenameStem("correction", 48000.0, 1023, FirPhase::Linear) ==
          "correction_48000Hz_1023taps_lin");
    CHECK(firFilenameStem("correction", 96000.0, 4095, FirPhase::Minimum) ==
          "correction_96000Hz_4095taps_min");
    CHECK(firFilenameStem("stage-left", 44100.0, 8, FirPhase::Linear) ==
          "stage-left_44100Hz_8taps_lin");
}
