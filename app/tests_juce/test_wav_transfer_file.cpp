// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H11 W3 (docs/dsp/2026-11-10-g22-offline-wav-transfer.md D1, D2, D6-D8): LOAD WAV and ANALYSE
// through the real MainComponent, from real WAV/AIFF files the test writes with JUCE's own
// writer. W2's closed-form numbers (app/tests/test_wav_transfer.cpp) are reached here through
// the whole stack: file decode -> WavFileSource -> WavTransferJob -> TraceLibrary group WAV.
//
// EXPECTED VALUE. As in W2: meas[n] = (h * ref)[n - D], h the +6 dB / Q=1 / 1 kHz RBJ peaking
// biquad, so the stored H is `biquadResponse` with no residual delay slope. Never a printed value.
//
// TOLERANCE, DERIVED. W2's two terms, copied (its derivation is in test_wav_transfer.cpp):
//   eps  = 4 sqrt(4/3) pi M1 / (N sqrt(Neff))      leakage of the IR tail across the window edge
//   tol  = eps + 1e-3                              + the float32 FFT floor
// plus ONE new term for a 24-bit file. Both channels are scaled by the SAME gain g before the
// write, so H is unchanged exactly; rounding to the 24-bit grid then adds white noise of variance
// q^2/12 per sample, q = 2^-23, uncorrelated with the programme. Against pink of variance s^2 on
// a 1/f density over fL..fs/2 (fL >= 1 Hz, so ln(fs/2 / fL) <= 10.1), the noise-to-signal power
// ratio is largest at the TOP of the compared band (16 kHz):
//   r = (q^2/12 / (fs/2)) * 16000 * 10.1 / s^2     (computed below from the measured s).
// That noise sits on the reference (H1 bias -r in power, so about r in magnitude)
// and on the measurement (zero-mean, rms sqrt(r)/sqrt(Neff) per bin); 4x the second and 1x the
// first make `quantisationEps`. It is asserted to be below 1e-3 so the term cannot hide a loophole:
// at -12 dBFS pink it is about 1e-6, i.e. a 24-bit file is as good as a float one for this job.
// A float32 file is exact for float32 data (the whole pipeline is single precision), so it adds 0.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "WavFileWriter.h"
#include "WavTransferFixture.h"
#include "measure/WavFileSource.h"
#include "rta/dsp/BiquadResponse.h"
#include "trace/TraceGroups.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

namespace {

using Access = MainComponentTestAccess;
constexpr const char* kTag = "[wav_transfer_file]";
constexpr std::size_t kSamples = 480000;  // 10 s at 48 kHz, the length W2's closed-form cases use
constexpr std::size_t kFftSize = 16384;

/// A file in the temp folder that deletes itself.
struct TempFile {
    juce::File file;
    explicit TempFile(const juce::String& name)
        : file(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("rta_h11_" + name)) {
        file.deleteFile();
    }
    ~TempFile() { file.deleteFile(); }
};

/// The same gain on every channel, so no channel clips and H is unchanged.
void scaleToHeadroom(std::vector<std::vector<float>>& channels) {
    float peak = 0.0f;
    for (const auto& c : channels) {
        for (float v : c) peak = std::max(peak, std::abs(v));
    }
    const float gain = 0.9f / peak;
    for (auto& c : channels) {
        for (float& v : c) v *= gain;
    }
}

/// ch0 = pink reference, ch2 = ch0 through the test system lagging by `delay`; ch1 and ch3 carry
/// other programmes so a wrong channel choice cannot pass.
std::vector<std::vector<float>> fourChannels(int delay) {
    std::vector<std::vector<float>> c(4);
    wavfix::makePair(kSamples, 5, [delay](std::size_t) { return delay; }, c[0], c[2]);
    c[1] = wavfix::pink(kSamples, 77);
    c[3] = wavfix::pink(kSamples, 78);
    return c;
}

double leakageEps() {
    rta::dsp::BiquadCascade cascade({wavfix::peaking()});
    double m1 = 0.0;
    for (int m = 0; m < 8192; ++m) m1 += m * std::abs(cascade.processSample(m == 0 ? 1.0 : 0.0));
    return std::sqrt(4.0 / 3.0) * std::numbers::pi * m1 / static_cast<double>(kFftSize);
}

/// The 24-bit term above, from the measured variance of the (scaled) reference.
double quantisationEps(const std::vector<float>& reference, double neff) {
    double sum = 0.0;
    for (float v : reference) sum += static_cast<double>(v) * v;
    const double variance = sum / static_cast<double>(reference.size());
    const double q = std::pow(2.0, -23.0);
    const double r = (q * q / 12.0 / 24000.0) * 16000.0 * 10.1 / variance;
    return r + 4.0 * std::sqrt(r / neff);
}

std::size_t countGroup(MainComponent& c, const std::string& group) {
    std::size_t n = 0;
    for (const auto& e : Access::libraryForTest(c).entries()) n += e.group == group ? 1 : 0;
    return n;
}

const rta::trace::Trace& onlyWavTrace(MainComponent& c) {
    for (const auto& e : Access::libraryForTest(c).entries()) {
        if (e.group == rta::trace::kWavGroup) return *Access::libraryForTest(c).trace(e.traceId);
    }
    FAIL("no WAV trace in the library");
    std::abort();
}

void requireClosedForm(const rta::trace::Trace& trace, double extraEps, double neff) {
    const double eps = 4.0 * leakageEps() / std::sqrt(neff) + extraEps;
    const double tolRel = eps + 1e-3;
    REQUIRE(trace.has(rta::trace::Field::Phase));
    REQUIRE(trace.has(rta::trace::Field::Coherence));
    const auto coeffs = wavfix::peaking();
    const auto mag = trace.field(rta::trace::Field::Magnitude);
    const auto phase = trace.field(rta::trace::Field::Phase);
    const double binHz = trace.binHz();
    for (std::size_t k = static_cast<std::size_t>(100.0 / binHz); k <= static_cast<std::size_t>(16000.0 / binHz);
         ++k) {
        const std::complex<double> h = rta::dsp::biquadResponse(coeffs, 2.0 * std::numbers::pi * k * binHz / wavfix::kFs);
        const double dbError = std::abs(mag[k] - 20.0 * std::log10(std::abs(h)));
        const double phaseError =
            std::abs(std::arg(std::polar(1.0, static_cast<double>(phase[k])) * std::conj(h) / std::abs(h)));
        REQUIRE(dbError <= 20.0 / std::log(10.0) * tolRel);
        REQUIRE(phaseError <= tolRel);
    }
}

/// Load, pick channels 1 and 3 (1-based), analyse the whole file, wait.
void analyseWhole(MainComponent& c, const juce::File& file, int refCh, int measCh) {
    REQUIRE(Access::performLoadWavForTest(c, file));
    auto& wav = Access::wavForTest(c);
    wav.setControlsForTest(refCh, measCh, 0.0, 0.0);
    wav.analyseClicked();
    CHECK(wav.readoutForTest() == "analysing ...");
    REQUIRE(wav.waitForJobForTest(60000));
}

}  // namespace

TEST_CASE("wav transfer file: a 4-channel 24-bit WAV, ch3 against ch1, matches the closed form", kTag) {
    auto channels = fourChannels(37);
    scaleToHeadroom(channels);
    TempFile temp("pcm24.wav");
    REQUIRE(wavfile::write(temp.file, wavfile::Container::Wav, wavfile::Sample::Pcm24, wavfix::kFs, channels));

    MainComponent c;
    analyseWhole(c, temp.file, 1, 3);
    REQUIRE(countGroup(c, rta::trace::kWavGroup) == 1);
    const auto& trace = onlyWavTrace(c);
    CHECK(trace.meta().appliedDelaySamples == 37);
    CHECK(trace.meta().deviceName == "WAV rta_h11_pcm24.wav ch3 vs ch1");
    CHECK(trace.meta().averagingType == "Cumulative");
    const double neff = trace.meta().effectiveAverages;
    const double quantisation = quantisationEps(channels[0], neff);
    CHECK(quantisation < 1e-3);  // the 24-bit term is small, not a loophole
    requireClosedForm(trace, quantisation, neff);

    // D8/D9: samples whole, ms one decimal, Neff one decimal, then where it went.
    const auto readout = Access::wavForTest(c).readoutForTest();
    CHECK(readout.startsWith("delay 37 samples (0.8 ms), "));
    CHECK(readout.contains(" trust, "));
    CHECK(readout.endsWith(" averages -> stored WAV rta_h11_pcm24.wav ch3 vs ch1"));
}

TEST_CASE("wav transfer file: a 4-channel float32 WAV with the measurement leading, D = -53", kTag) {
    const auto channels = fourChannels(-53);
    TempFile temp("float32.wav");
    REQUIRE(wavfile::write(temp.file, wavfile::Container::Wav, wavfile::Sample::Float32, wavfix::kFs, channels));

    MainComponent c;
    analyseWhole(c, temp.file, 1, 3);
    REQUIRE(countGroup(c, rta::trace::kWavGroup) == 1);
    const auto& trace = onlyWavTrace(c);
    CHECK(trace.meta().appliedDelaySamples == -53);
    requireClosedForm(trace, 0.0, trace.meta().effectiveAverages);
    CHECK(Access::wavForTest(c).readoutForTest().startsWith("delay -53 samples (-1.1 ms), "));
}

TEST_CASE("wav transfer file: a mono file says why and cannot be analysed", kTag) {
    TempFile temp("mono.wav");
    REQUIRE(wavfile::write(temp.file, wavfile::Container::Wav, wavfile::Sample::Pcm16, wavfix::kFs,
                           {wavfix::pink(48000, 3)}));
    MainComponent c;
    REQUIRE(Access::performLoadWavForTest(c, temp.file));
    auto& wav = Access::wavForTest(c);
    CHECK(wav.readoutForTest() ==
          "This file has 1 channel; a transfer function needs two (a reference and a measurement).");
    CHECK_FALSE(wav.analyseEnabledForTest());
    wav.analyseClicked();  // a press on a disabled button does nothing
    CHECK(wav.waitForJobForTest(1000));
    CHECK(countGroup(c, rta::trace::kWavGroup) == 0);
}

TEST_CASE("wav transfer file: a second ANALYSE while the first runs stores only the newest", kTag) {
    // ch1 is the reference; ch2 lags it by 11 samples and ch3 by 29. The first press asks for
    // ch2, the second (before any poll) for ch3: one trace, and it must be ch3's.
    std::vector<std::vector<float>> channels(3);
    wavfix::makePair(kSamples, 5, [](std::size_t) { return 11; }, channels[0], channels[1]);
    std::vector<float> unused;
    wavfix::makePair(kSamples, 5, [](std::size_t) { return 29; }, unused, channels[2]);
    TempFile temp("twice.wav");
    REQUIRE(wavfile::write(temp.file, wavfile::Container::Wav, wavfile::Sample::Float32, wavfix::kFs, channels));

    MainComponent c;
    REQUIRE(Access::performLoadWavForTest(c, temp.file));
    auto& wav = Access::wavForTest(c);
    wav.setControlsForTest(1, 2, 0.0, 0.0);
    wav.analyseClicked();
    wav.setControlsForTest(1, 3, 0.0, 0.0);
    wav.analyseClicked();
    REQUIRE(wav.waitForJobForTest(60000));
    REQUIRE(countGroup(c, rta::trace::kWavGroup) == 1);
    CHECK(onlyWavTrace(c).meta().appliedDelaySamples == 29);
    CHECK(onlyWavTrace(c).meta().deviceName == "WAV rta_h11_twice.wav ch3 vs ch1");
}

TEST_CASE("wav transfer file: an AIFF loads and decodes the channel it is asked for", kTag) {
    std::vector<std::vector<float>> channels(2, std::vector<float>(4800));
    for (std::size_t i = 0; i < channels[0].size(); ++i) {
        channels[0][i] = 0.25f * static_cast<float>(std::sin(0.01 * static_cast<double>(i)));
        channels[1][i] = -0.5f + 1e-4f * static_cast<float>(i);  // a ramp, so a channel mix-up shows
    }
    TempFile temp("tone.aiff");
    REQUIRE(wavfile::write(temp.file, wavfile::Container::Aiff, wavfile::Sample::Pcm24, 44100.0, channels));

    MainComponent c;
    REQUIRE(Access::performLoadWavForTest(c, temp.file));
    auto& wav = Access::wavForTest(c);
    CHECK(wav.fileTextForTest() == "rta_h11_tone.aiff - 2 ch, 44100 Hz, 0.1 s");
    CHECK(wav.analyseEnabledForTest());

    const auto source = rta::measure::WavFileSource::open(temp.file);
    REQUIRE(source != nullptr);
    CHECK(source->numChannels() == 2);
    CHECK(source->lengthSamples() == 4800);
    std::vector<float> block(100);
    for (int channel = 0; channel < 2; ++channel) {
        REQUIRE(source->read(channel, 1000, block));
        for (std::size_t i = 0; i < block.size(); ++i) {
            // 24-bit rounding: half a step, q = 2^-23, plus the float32 representation of the input.
            REQUIRE(std::abs(block[i] - channels[static_cast<std::size_t>(channel)][1000 + i]) <= 2.0e-7);
        }
    }
    CHECK_FALSE(source->read(0, 4750, block));  // 4750 + 100 runs past the end: refused, never padded
}

TEST_CASE("wav transfer file: a file that is not audio is refused on load", kTag) {
    TempFile temp("notes.wav");
    REQUIRE(temp.file.replaceWithText("this is not a RIFF file"));
    MainComponent c;
    CHECK_FALSE(Access::performLoadWavForTest(c, temp.file));
    CHECK(Access::wavForTest(c).readoutForTest() ==
          "wav: rta_h11_notes.wav could not be read (WAV and AIFF only)");
    CHECK_FALSE(Access::wavForTest(c).analyseEnabledForTest());
}

TEST_CASE("wav transfer file: a delay that rounds to 0.0 ms reads \"0.0 ms\", never \"-0.0 ms\"", kTag) {
    const auto channels = fourChannels(-1);  // -1 sample = -0.0208 ms at 48 kHz
    TempFile temp("lead1.wav");
    REQUIRE(wavfile::write(temp.file, wavfile::Container::Wav, wavfile::Sample::Float32, wavfix::kFs, channels));
    MainComponent c;
    analyseWhole(c, temp.file, 1, 3);
    REQUIRE(countGroup(c, rta::trace::kWavGroup) == 1);
    CHECK(onlyWavTrace(c).meta().appliedDelaySamples == -1);
    const auto readout = Access::wavForTest(c).readoutForTest();
    CHECK(readout.startsWith("delay -1 samples (0.0 ms), "));
    CHECK_FALSE(readout.contains("-0.0"));
}

TEST_CASE("wav transfer file: the stored WAV trace takes the lowest shade unused across ALL entries", kTag) {
    const auto channels = fourChannels(37);
    TempFile temp("shade.wav");
    REQUIRE(wavfile::write(temp.file, wavfile::Container::Wav, wavfile::Sample::Float32, wavfix::kFs, channels));

    MainComponent c;
    auto& library = Access::libraryForTest(c);
    // A SWEEP/STORE trace already holds shade 0 of its own group.
    rta::trace::CaptureMeta meta;
    meta.id = "shade-test-sweep";
    meta.sampleRate = wavfix::kFs;
    meta.fftSize = 8;
    auto sweep = rta::trace::Trace::make(meta, std::vector<float>(5, -20.0f));
    REQUIRE(sweep.has_value());
    REQUIRE_FALSE(library.add(std::move(*sweep), "sweep", rta::trace::kSweepGroup).empty());
    REQUIRE(library.entry("shade-test-sweep")->shadeIndex == 0);

    analyseWhole(c, temp.file, 1, 3);
    int wavShade = -1;
    for (const auto& e : library.entries()) {
        if (e.group == rta::trace::kWavGroup) wavShade = e.shadeIndex;
    }
    CHECK(wavShade == 1);  // not 0: that is the SWEEP trace's colour on TRANSFER
}

TEST_CASE("wav transfer file: a WAV with no samples says so, not \"WAV and AIFF only\"", kTag) {
    // A canonical 44-byte header: mono, 16-bit, 48 kHz, a data chunk of 0 bytes.
    const unsigned char header[44] = {'R', 'I', 'F', 'F', 36, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
                                      16,  0,   0,   0,   1,  0, 1, 0, 0x80, 0xBB, 0, 0, 0x00, 0x77, 0x01, 0,
                                      2,   0,   16,  0,   'd', 'a', 't', 'a', 0, 0, 0, 0};
    TempFile temp("empty.wav");
    REQUIRE(temp.file.replaceWithData(header, sizeof header));
    MainComponent c;
    CHECK_FALSE(Access::performLoadWavForTest(c, temp.file));
    CHECK(Access::wavForTest(c).readoutForTest() == "wav: rta_h11_empty.wav has no samples");
    CHECK_FALSE(Access::wavForTest(c).analyseEnabledForTest());
}
