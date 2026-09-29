// SPDX-License-Identifier: AGPL-3.0-or-later
// L7-EQ UI wave A, task T6 (docs/plans/2026-09-29-eq-ui-lane-plan.md): the EQ
// exports, through MainComponentEq's perform* halves (the chooser callbacks
// call exactly these; a native dialog cannot run headless). Expected values
// are what the plan and the FIR / EQ records state: the header keys, the file
// stem `eq_<fs>Hz_<N>taps_<lin|min>`, one coefficient per line, float32 mono
// WAV bitwise equal to the designed taps, and the filter list keeping its
// `applied` token (EQ record Sec.7, amendment 2).
//
// N is the plan's 4096 (D9); core's even-N design is exact since the
// half-sample fix (FIR record Sec.4, amendment 2026-09-29).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "EqTraceFixture.h"
#include "MainComponentEq.h"
#include "export/EqTextExport.h"
#include "measure/EqFirDesign.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kTaps = 4096;

/// A scratch folder that removes itself, so a failed assertion cannot leave a
/// stale file for the next run to read (same convention as test_fir_wav.cpp).
struct TempFolder {
    juce::File dir;
    explicit TempFolder(const char* name)
        : dir(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("rta-eq-export-test").getChildFile(name)) {
        dir.deleteRecursively();
        dir.createDirectory();
    }
    ~TempFolder() { dir.deleteRecursively(); }
};

/// A controller with a picked bump trace, AUTO EQ run, and the FIR questions
/// answered. `answer` false leaves them unasked.
struct Rig {
    rta::trace::TraceLibrary library;
    MainComponentEq eq;
    explicit Rig(bool answer = true, rta::dsp::FirPhase phase = rta::dsp::FirPhase::Linear) {
        const auto id = library.add(eqfixture::makeBumpTrace("bump"), "TRANSFER @ 12:00:00", "TRANSFER");
        auto& model = eq.modelForTest();
        REQUIRE(model.selectMeasurement(library, id));
        model.runAutoEq();
        REQUIRE_FALSE(model.session().committed().empty());
        if (answer) {
            model.setFirPhase(phase);
            model.setFirTaps(kTaps);
        }
    }
};

std::string readText(const juce::File& file) { return file.loadFileAsString().toStdString(); }

}  // namespace

TEST_CASE("EXPORT FIR TXT writes the header keys and one coefficient per line", "[main_component_eq_export]") {
    TempFolder folder("txt");
    Rig rig;
    const auto file = folder.dir.getChildFile("out.txt");
    REQUIRE(rig.eq.performExportFirText(file));

    const std::string text = readText(file);
    CHECK(text.find("sample_rate_hz=48000") != std::string::npos);
    CHECK(text.find("taps=4096") != std::string::npos);
    CHECK(text.find("phase=linear") != std::string::npos);
    CHECK(text.find("normalization=as_designed") != std::string::npos);  // D12's default

    // Everything after the marker is one float per line: exactly kTaps of them.
    const auto marker = text.find("# --- coefficients follow ---\n");
    REQUIRE(marker != std::string::npos);
    std::istringstream body(text.substr(marker + std::string("# --- coefficients follow ---\n").size()));
    std::size_t lines = 0;
    for (std::string line; std::getline(body, line);) ++lines;
    CHECK(lines == kTaps);

    // UTF-8 with no BOM, and "\n" left alone (replaceWithText would give "\r\n").
    juce::MemoryBlock bytes;
    REQUIRE(file.loadFileAsData(bytes));
    REQUIRE(bytes.getSize() > 3);
    const auto* raw = static_cast<const unsigned char*>(bytes.getData());
    CHECK_FALSE((raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF));
    CHECK(text.find('\r') == std::string::npos);
}

TEST_CASE("the FIR file stem is eq_<fs>Hz_<N>taps_<lin|min>", "[main_component_eq_export]") {
    Rig linear(true, rta::dsp::FirPhase::Linear);
    CHECK(linear.eq.defaultFirFile(".txt").getFileName() == "eq_48000Hz_4096taps_lin.txt");
    CHECK(linear.eq.defaultFirFile(".wav").getFileName() == "eq_48000Hz_4096taps_lin.wav");
    CHECK(linear.eq.defaultFirFile(".txt").getParentDirectory().getFileName() == "fir");

    Rig minimum(true, rta::dsp::FirPhase::Minimum);
    CHECK(minimum.eq.defaultFirFile(".txt").getFileName() == "eq_48000Hz_4096taps_min.txt");

    Rig unanswered(false);
    CHECK(unanswered.eq.defaultFirFile(".txt") == juce::File{});  // no stem until both are asked
}

TEST_CASE("EXPORT FIR WAV reads back as 1 channel, float32, 48000 Hz, bitwise the designed taps",
          "[main_component_eq_export]") {
    TempFolder folder("wav");
    Rig rig;
    const auto file = folder.dir.getChildFile("out.wav");
    REQUIRE(rig.eq.performExportFirWav(file));

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(file.createInputStream().release(), true));
    REQUIRE(reader != nullptr);
    CHECK(reader->numChannels == 1u);
    CHECK(reader->sampleRate == 48000.0);
    CHECK(reader->usesFloatingPointData);
    CHECK(reader->bitsPerSample == 32u);
    REQUIRE(static_cast<std::size_t>(reader->lengthInSamples) == kTaps);

    // The taps the WAV must hold are the design of the filters NOT yet applied
    // (D11): recomputed here through the same public designer.
    const auto expected = rta::measure::designEqFir(rig.eq.modelForTest().session().committed(), 48000.0, kTaps,
                                                    rta::dsp::FirPhase::Linear);
    juce::AudioBuffer<float> buffer(1, static_cast<int>(kTaps));
    REQUIRE(reader->read(&buffer, 0, static_cast<int>(kTaps), 0, true, false));
    bool bitwise = true;
    for (std::size_t n = 0; n < kTaps; ++n) bitwise = bitwise && (buffer.getSample(0, static_cast<int>(n)) == expected.taps[n]);
    CHECK(bitwise);
}

TEST_CASE("EXPORT LIST round-trips through parseFilterList, keeping the applied token",
          "[main_component_eq_export]") {
    TempFolder folder("list");
    Rig rig;
    auto& model = rig.eq.modelForTest();
    REQUIRE(model.session().committed().size() >= 1);
    model.setApplied(0, true);

    const auto file = folder.dir.getChildFile("filters.txt");
    REQUIRE(rig.eq.performExportList(file));
    const auto parsed = rta::eqexport::parseFilterList(readText(file));
    CHECK(parsed.rejectedLines.empty());
    const auto committed = model.session().committed();
    REQUIRE(parsed.filters.size() == committed.size());
    for (std::size_t i = 0; i < committed.size(); ++i) {
        CAPTURE(i);
        // The list prints fc whole hertz, Q two decimals, gain one decimal
        // (EqTextExport.h): half a unit in the last place is the round trip.
        CHECK(parsed.filters[i].spec.type == committed[i].spec.type);
        CHECK(parsed.filters[i].spec.fcHz == Catch::Approx(committed[i].spec.fcHz).margin(0.5));
        CHECK(parsed.filters[i].spec.q == Catch::Approx(committed[i].spec.q).margin(0.005));
        CHECK(parsed.filters[i].spec.gainDb == Catch::Approx(committed[i].spec.gainDb).margin(0.05));
        CHECK(parsed.filters[i].applied == committed[i].applied);
    }
    CHECK(parsed.filters.front().applied);
}

TEST_CASE("EXPORT FIR refuses, naming the reason, until both questions are answered", "[main_component_eq_export]") {
    TempFolder folder("refuse");
    Rig rig(false);  // filters exist; phase and length were never asked
    const auto text = folder.dir.getChildFile("no.txt");
    const auto wav = folder.dir.getChildFile("no.wav");

    CHECK_FALSE(rig.eq.performExportFirText(text));
    CHECK(rig.eq.modelForTest().status().find("FIR PHASE and FIR LENGTH") != std::string::npos);
    CHECK_FALSE(rig.eq.performExportFirWav(wav));
    CHECK_FALSE(text.existsAsFile());
    CHECK_FALSE(wav.existsAsFile());

    rig.eq.modelForTest().setFirPhase(rta::dsp::FirPhase::Linear);  // only one of the two
    CHECK_FALSE(rig.eq.performExportFirText(text));
    CHECK_FALSE(text.existsAsFile());
}

TEST_CASE("nothing to realise: every filter already applied means no FIR export", "[main_component_eq_export]") {
    TempFolder folder("applied");
    Rig rig;
    auto& model = rig.eq.modelForTest();
    for (std::size_t i = 0; i < model.session().committed().size(); ++i) model.setApplied(i, true);
    // D11: an applied filter is already in the rig -- exporting it would put
    // its correction there twice.
    CHECK_FALSE(model.canExportFir());
    CHECK_FALSE(rig.eq.performExportFirText(folder.dir.getChildFile("x.txt")));
    CHECK(rig.eq.performExportList(folder.dir.getChildFile("x_list.txt")));  // the list still records them
}

TEST_CASE("a failed write reports, and never claims success", "[main_component_eq_export]") {
    Rig rig;
    // A file whose "parent" is an existing FILE cannot be created.
    TempFolder folder("blocked");
    const auto blocker = folder.dir.getChildFile("blocker");
    REQUIRE(blocker.replaceWithText("x"));
    CHECK_FALSE(rig.eq.performExportFirText(blocker.getChildFile("out.txt")));
    CHECK(rig.eq.modelForTest().status().find("FAILED") != std::string::npos);
}
