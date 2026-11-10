// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H12 T4 (docs/dsp/2026-11-10-g22b-two-file-wav-drift.md E1, E2, E9): LOAD WAV + LOAD MEAS WAV +
// ANALYSE through the real MainComponent, from real float32 WAV files the test writes with
// JUCE's own writer. T3's pure analysis (app/tests/test_wav_pair_transfer*.cpp) is reached here
// through the whole stack: file decode -> WavFileSource (incl. the `bext` time reference) ->
// WavTransferJob -> TraceLibrary group WAV -> the readout.
//
// FIXTURES are T3's (app/tests/WavPairFixture.h): the measurement is made by an independent
// 512-tap Blackman-Harris sinc, never by the resampler under test.
//
// EXPECTED VALUES. Truth comes from the fixture's own parameters, never from a printed value:
//   ppm      the clock error eps * 1e6, within T3's budget: kDelta / rangeSamples * 1e6 with
//            kDelta = 0.1 sample (a 0.1-sample delay budget over a 6.5 s range is 0.32 ppm);
//   offset   t(rangeStart) - rangeStart = (b + eps * start) seconds, printed to 3 decimals;
//   bext     a time reference is the wall-clock sample count of a file's FIRST sample, so the offset
//            (the measurement-file index of reference sample 0) is REFERENCE minus MEASUREMENT: a
//            measurement that began b samples earlier carries ref - b, one that began later ref + D.
//            The reference's is 1000000 so every case stays positive and the chunk non-zero (JUCE omits
//            a bext chunk whose time reference is 0).
// FILES stay small: an 8 s reference; the 14 s case is a zero-clock-error integer shift so the
// fixture costs no interpolation.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "WavFileWriter.h"
#include "WavPairFixture.h"
#include "trace/TraceGroups.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace {

using Access = MainComponentTestAccess;
constexpr const char* kTag = "[wav_pair_file]";
constexpr double kFs = wavfix::kFs;
constexpr std::size_t kRefSamples = 8 * 48000;
constexpr double kDeltaSamples = 0.1;  // T3's delay budget (app/tests/test_wav_pair_transfer.cpp kDelta)
constexpr std::int64_t kRefBext = 1000000;

struct TempFile {
    juce::File file;
    explicit TempFile(const juce::String& name)
        : file(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("rta_h12_" + name)) {
        file.deleteFile();
    }
    ~TempFile() { file.deleteFile(); }
    [[nodiscard]] std::string name() const { return file.getFileName().toStdString(); }
};

bool writeFloat(const juce::File& file, const std::vector<std::vector<float>>& channels,
                std::optional<std::int64_t> bext = std::nullopt, double rate = kFs) {
    return wavfile::write(file, wavfile::Container::Wav, wavfile::Sample::Float32, rate, channels, bext);
}

/// +80 ppm, the measurement recorded 3.2 s late, through the +6 dB biquad (computed once: the
/// 512-tap interpolation of 11 s is the slow part).
const wavpair::Pair& driftPair() {
    static const wavpair::Pair p =
        wavpair::makePair(kRefSamples, 11 * 48000, wavpair::linearClock(3.2 * kFs, 80e-6), 11, true);
    return p;
}

/// A zero-drift pair: the measurement is the reference shifted by `shiftSeconds`, integer samples.
wavpair::Pair shiftedPair(double shiftSeconds, double measSeconds) {
    return wavpair::shiftedPair(kRefSamples, static_cast<std::size_t>(measSeconds * kFs),
                                static_cast<std::ptrdiff_t>(std::llround(shiftSeconds * kFs)));
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

/// Loads both files, picks ch1 of each, sets the range and waits for the worker.
void analysePair(MainComponent& c, const juce::File& ref, const juce::File& meas, double start, double length) {
    REQUIRE(Access::performLoadWavForTest(c, ref));
    REQUIRE(Access::performLoadMeasWavForTest(c, meas));
    auto& wav = Access::wavForTest(c);
    wav.setControlsForTest(1, 1, start, length);
    wav.analyseClicked();
    CHECK(wav.readoutForTest() == "analysing ...");
    REQUIRE(wav.waitForJobForTest(120000));
}

/// The ppm in "clock +80.00 ppm corrected".
double readoutPpm(const juce::String& readout) {
    const int at = readout.indexOf("clock ");
    REQUIRE(at >= 0);
    return readout.substring(at + 6).getDoubleValue();
}

/// A 2-channel file: ch1 pink, ch2 the same through the test system lagging 37 samples.
std::vector<std::vector<float>> twoChannels() {
    std::vector<std::vector<float>> c(2);
    wavfix::makePair(480000, 5, [](std::size_t) { return 37; }, c[0], c[1]);
    return c;
}

}  // namespace

TEST_CASE("wav pair file: +80 ppm and a 3.2 s offset with bext references -> one trace, ppm and offset on the readout",
          kTag) {
    const auto& pair = driftPair();
    TempFile ref("a_ref.wav"), meas("a_meas.wav");
    REQUIRE(writeFloat(ref.file, pair.reference.channels, kRefBext));
    REQUIRE(writeFloat(meas.file, pair.measurement.channels, kRefBext - static_cast<std::int64_t>(3.2 * kFs)));

    MainComponent c;
    analysePair(c, ref.file, meas.file, 0.5, 6.5);
    REQUIRE(countGroup(c, rta::trace::kWavGroup) == 1);
    const auto& wav = Access::wavForTest(c);
    const std::string name = "WAV " + meas.name() + " ch1 vs " + ref.name() + " ch1";
    CHECK(onlyWavTrace(c).meta().deviceName == name);
    CHECK(wav.fileTextForTest().contains(ref.name()));
    CHECK(wav.fileTextForTest().contains(meas.name()));

    const auto readout = wav.readoutForTest();
    CHECK(readout.contains("ppm corrected"));
    CHECK(std::abs(readoutPpm(readout) - 80.0) <= kDeltaSamples / (6.5 * kFs) * 1e6 + 0.005);  // + the %.2f rounding
    CHECK(readout.contains(", offset 3.200 s -> stored "));
    CHECK(readout.endsWith(name));
}

TEST_CASE("wav pair file: a 14 s offset is found through the bext prior and refused without it", kTag) {
    const auto begin = std::chrono::steady_clock::now();
    const auto pair = shiftedPair(14.0, 23.0);
    TempFile ref("b_ref.wav"), meas("b_meas.wav"), refPlain("b_ref_plain.wav"), measPlain("b_meas_plain.wav");
    REQUIRE(writeFloat(ref.file, pair.reference.channels, kRefBext));
    REQUIRE(writeFloat(meas.file, pair.measurement.channels, kRefBext - static_cast<std::int64_t>(14.0 * kFs)));
    REQUIRE(writeFloat(refPlain.file, pair.reference.channels));
    REQUIRE(writeFloat(measPlain.file, pair.measurement.channels));

    {
        MainComponent c;
        analysePair(c, ref.file, meas.file, 0.0, 8.0);
        REQUIRE(countGroup(c, rta::trace::kWavGroup) == 1);
        CHECK(Access::wavForTest(c).readoutForTest().contains("offset 14.000 s"));
    }
    {
        MainComponent c;
        analysePair(c, refPlain.file, measPlain.file, 0.0, 8.0);
        CHECK(countGroup(c, rta::trace::kWavGroup) == 0);
        const auto readout = Access::wavForTest(c).readoutForTest();
        CHECK(readout.startsWith("The two files could not be lined up within 10.000 s"));
    }
    std::printf("[wav pair 14 s] wall %.1f s\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count());
}

TEST_CASE("wav pair file: a measurement that starts 14 s LATER (negative offset) is found through the bext prior",
          kTag) {
    // meas[m] = y[m + 14 s]: the measurement began 14 s after the reference, so its time reference is the
    // reference's PLUS 14 s and the offset is -14 s. Without the prior the +-10 s search around 0 misses it.
    const auto pair = wavpair::shiftedPair(20 * 48000, 9 * 48000, -static_cast<std::ptrdiff_t>(14 * 48000));
    TempFile ref("h_ref.wav"), meas("h_meas.wav"), refPlain("h_ref_plain.wav"), measPlain("h_meas_plain.wav");
    REQUIRE(writeFloat(ref.file, pair.reference.channels, kRefBext));
    REQUIRE(writeFloat(meas.file, pair.measurement.channels, kRefBext + static_cast<std::int64_t>(14.0 * kFs)));
    REQUIRE(writeFloat(refPlain.file, pair.reference.channels));
    REQUIRE(writeFloat(measPlain.file, pair.measurement.channels));
    {
        MainComponent c;
        analysePair(c, ref.file, meas.file, 14.5, 5.0);
        INFO(Access::wavForTest(c).readoutForTest());
        REQUIRE(countGroup(c, rta::trace::kWavGroup) == 1);
        CHECK(Access::wavForTest(c).readoutForTest().contains("offset -14.000 s"));
    }
    {
        MainComponent c;
        analysePair(c, refPlain.file, measPlain.file, 14.5, 5.0);
        CHECK(countGroup(c, rta::trace::kWavGroup) == 0);
        CHECK(Access::wavForTest(c).readoutForTest().startsWith("The two files could not be lined up within 10.000 s"));
    }
}

TEST_CASE("wav pair file: a bext reference on the measurement alone is no prior", kTag) {
    // The measurement says it started at 20.8 s of the day and the reference says nothing: taken as
    // "reference at 0" the search would be centred 20.8 s away and miss the true 3.2 s.
    const auto& pair = driftPair();
    TempFile ref("g_ref.wav"), meas("g_meas.wav");
    REQUIRE(writeFloat(ref.file, pair.reference.channels));
    REQUIRE(writeFloat(meas.file, pair.measurement.channels, 1000000));

    MainComponent c;
    analysePair(c, ref.file, meas.file, 0.5, 6.5);
    REQUIRE(countGroup(c, rta::trace::kWavGroup) == 1);
    CHECK(Access::wavForTest(c).readoutForTest().contains(", offset 3.200 s -> stored "));
}

TEST_CASE("wav pair file: different sample rates are refused on the readout", kTag) {
    TempFile ref("c_ref.wav"), meas("c_meas.wav");
    REQUIRE(writeFloat(ref.file, {wavfix::pink(96000, 3)}, std::nullopt, 48000.0));
    REQUIRE(writeFloat(meas.file, {wavfix::pink(88200, 4)}, std::nullopt, 44100.0));

    MainComponent c;
    analysePair(c, ref.file, meas.file, 0.0, 1.5);
    CHECK(countGroup(c, rta::trace::kWavGroup) == 0);
    CHECK(Access::wavForTest(c).readoutForTest() ==
          juce::String(ref.name()) + " is 48000 Hz and " + meas.name() +
              " is 44100 Hz: both files need the same sample rate. Convert one of them first.");
}

TEST_CASE("wav pair file: CLEAR MEAS returns to the one-file path", kTag) {
    const auto channels = twoChannels();
    TempFile one("d_one.wav"), other("d_other.wav");
    REQUIRE(writeFloat(one.file, channels));
    REQUIRE(writeFloat(other.file, {wavfix::pink(48000, 3), wavfix::pink(48000, 4), wavfix::pink(48000, 5)}));

    MainComponent c;
    auto& wav = Access::wavForTest(c);
    REQUIRE(Access::performLoadWavForTest(c, one.file));
    CHECK(wav.measChannelCountForTest() == 2);
    REQUIRE(Access::performLoadMeasWavForTest(c, other.file));
    CHECK(wav.hasMeasFileForTest());
    CHECK(wav.measChannelCountForTest() == 3);  // the second file's channels
    CHECK(wav.fileTextForTest().contains(other.name()));

    Access::clearMeasWavForTest(c);
    CHECK_FALSE(wav.hasMeasFileForTest());
    CHECK(wav.measChannelCountForTest() == 2);
    CHECK_FALSE(wav.fileTextForTest().contains(other.name()));
    wav.setControlsForTest(1, 2, 0.0, 0.0);
    wav.analyseClicked();
    REQUIRE(wav.waitForJobForTest(60000));
    REQUIRE(countGroup(c, rta::trace::kWavGroup) == 1);
    CHECK(onlyWavTrace(c).meta().deviceName == "WAV " + one.name() + " ch2 vs ch1");
    CHECK_FALSE(wav.readoutForTest().contains("clock"));
}

TEST_CASE("wav pair file: the same file as the measurement is the one-file path", kTag) {
    TempFile one("e_one.wav");
    REQUIRE(writeFloat(one.file, twoChannels()));

    MainComponent c;
    auto& wav = Access::wavForTest(c);
    REQUIRE(Access::performLoadWavForTest(c, one.file));
    REQUIRE(Access::performLoadMeasWavForTest(c, one.file));
    CHECK_FALSE(wav.hasMeasFileForTest());
    wav.setControlsForTest(1, 2, 0.0, 0.0);
    wav.analyseClicked();
    REQUIRE(wav.waitForJobForTest(60000));
    REQUIRE(countGroup(c, rta::trace::kWavGroup) == 1);
    CHECK(onlyWavTrace(c).meta().deviceName == "WAV " + one.name() + " ch2 vs ch1");
    CHECK_FALSE(wav.readoutForTest().contains("offset"));
}

TEST_CASE("wav pair file: a shared clock (0 ppm, 1.7 s offset) says so", kTag) {
    const auto pair = shiftedPair(1.7, 11.0);
    TempFile ref("f_ref.wav"), meas("f_meas.wav");
    REQUIRE(writeFloat(ref.file, pair.reference.channels));
    REQUIRE(writeFloat(meas.file, pair.measurement.channels));

    MainComponent c;
    analysePair(c, ref.file, meas.file, 0.5, 6.5);
    REQUIRE(countGroup(c, rta::trace::kWavGroup) == 1);
    const auto readout = Access::wavForTest(c).readoutForTest();
    CHECK(readout.contains(", shared clock, offset 1.700 s -> stored "));
    CHECK_FALSE(readout.contains("ppm corrected"));
}

TEST_CASE("wav pair file: LENGTH is empty in pair mode and analyses to the end of the overlap", kTag) {
    // A measurement 30 % shorter than the reference: the reference's own length would run past the overlap.
    const auto pair = shiftedPair(1.0, 7.0);
    TempFile ref("i_ref.wav"), meas("i_meas.wav");
    REQUIRE(writeFloat(ref.file, pair.reference.channels));
    REQUIRE(writeFloat(meas.file, pair.measurement.channels));

    MainComponent c;
    auto& wav = Access::wavForTest(c);
    REQUIRE(Access::performLoadWavForTest(c, ref.file));
    CHECK(wav.lengthTextForTest() == "8.00");  // one file: the whole file, as before
    REQUIRE(Access::performLoadMeasWavForTest(c, meas.file));
    CHECK(wav.lengthTextForTest().isEmpty());  // two files: to the end of the overlap
    wav.setControlsForTest(1, 1, 0.5, 0.0);
    wav.analyseClicked();
    REQUIRE(wav.waitForJobForTest(120000));
    INFO(wav.readoutForTest());
    REQUIRE(countGroup(c, rta::trace::kWavGroup) == 1);
    CHECK(wav.readoutForTest().contains(", offset 1.000 s -> stored "));
    // Back to one file: the prefill returns.
    Access::clearMeasWavForTest(c);
    CHECK(wav.lengthTextForTest() == "8.00");
}
