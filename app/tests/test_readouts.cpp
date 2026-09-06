// SPDX-License-Identifier: AGPL-3.0-or-later
//
// deviceTypeNotice and readoutLine are pure functions precisely so the three
// mixed formatting rules on one readout line (whole-Hz, one-decimal-dB,
// plain-integer-frames -- CLAUDE.md "Reading out numbers") can be pinned
// here instead of eyeballed from a screenshot. The exact four-space
// separators are asserted too: tools/snapshot.cpp renders rta-view.png
// specifically so it is reviewable as a byte-for-byte diff, and a stray
// space would move every pixel to its right.

#include <catch2/catch_test_macros.hpp>

#include "view/Readouts.h"

#include "measure/Snapshot.h"

using namespace rta::view;
using rta::measure::BandReading;
using rta::measure::Snapshot;

TEST_CASE("deviceTypeNotice is empty when requested and actual agree", "[readouts]") {
    CHECK(deviceTypeNotice("ASIO", "ASIO").empty());
}

TEST_CASE("deviceTypeNotice is empty when requested is empty", "[readouts]") {
    CHECK(deviceTypeNotice("", "Windows Audio").empty());
}

TEST_CASE("deviceTypeNotice is empty when actual is empty", "[readouts]") {
    CHECK(deviceTypeNotice("ASIO", "").empty());
}

TEST_CASE("deviceTypeNotice names the requested type first when they differ", "[readouts]") {
    const auto notice = deviceTypeNotice("ASIO", "Windows Audio");
    CHECK_FALSE(notice.empty());
    CHECK(notice.find("ASIO") != std::string::npos);
    CHECK(notice.find("Windows Audio") != std::string::npos);
    // "requested named first" -- ASIO's position in the string precedes
    // Windows Audio's, not just "both appear somewhere".
    CHECK(notice.find("ASIO") < notice.find("Windows Audio"));
}

namespace {

Snapshot makeSnapshot(float peakHz, float peakDb, std::uint64_t frames, bool withBand) {
    Snapshot snap;
    snap.peakBandCentreHz = peakHz;
    snap.peakBandLevelDb = peakDb;
    snap.framesAnalysed = frames;
    if (withBand) {
        BandReading band;
        band.centreHz = peakHz;
        band.levelDb = peakDb;
        snap.bands.push_back(band);
    }
    return snap;
}

}  // namespace

TEST_CASE("readoutLine on a null snapshot shows all three placeholders", "[readouts]") {
    CHECK(readoutLine(nullptr) == "-- Hz    -- dB    -- FRAMES");
}

TEST_CASE("readoutLine rounds the peak frequency to whole hertz", "[readouts]") {
    const auto snap = makeSnapshot(999.6f, -12.34f, 42, true);
    const auto line = readoutLine(&snap);
    CHECK(line.rfind("1000 Hz", 0) == 0);
}

TEST_CASE("readoutLine keeps one decimal on the peak level", "[readouts]") {
    const auto snap = makeSnapshot(999.6f, -12.34f, 42, true);
    const auto line = readoutLine(&snap);
    CHECK(line.find("-12.3 dB") != std::string::npos);
}

TEST_CASE("readoutLine ends with the plain integer frame count", "[readouts]") {
    const auto snap = makeSnapshot(999.6f, -12.34f, 42, true);
    const auto line = readoutLine(&snap);
    CHECK(line.size() >= 9);
    CHECK(line.substr(line.size() - 9) == "42 FRAMES");
}

TEST_CASE("readoutLine shows the frame count even with empty bands", "[readouts]") {
    const auto snap = makeSnapshot(999.6f, -12.34f, 42, false);
    const auto line = readoutLine(&snap);
    CHECK(line == "-- Hz    -- dB    42 FRAMES");
}

TEST_CASE("readoutLine uses exact four-space separators", "[readouts]") {
    const auto snap = makeSnapshot(1000.0f, 0.0f, 7, true);
    const auto line = readoutLine(&snap);
    CHECK(line == "1000 Hz    0.0 dB    7 FRAMES");
}

// --- B7: the multichannel readouts (record §6, §7; CLAUDE.md's readout
// rules) -- frequency a whole hertz, dB one decimal, coherence/R two
// decimals, a contributor count a bare integer pair. ---

TEST_CASE("formatHz rounds to the nearest whole hertz", "[readouts]") {
    CHECK(formatHz(1000.4) == "1000 Hz");
    CHECK(formatHz(999.6) == "1000 Hz");
    CHECK(formatHz(0.0) == "0 Hz");
}

TEST_CASE("formatTrim keeps one decimal and a sign", "[readouts]") {
    CHECK(formatTrim(-3.0) == "-3.0 dB");
    CHECK(formatTrim(3.0) == "3.0 dB");
    CHECK(formatTrim(0.0) == "0.0 dB");
}

TEST_CASE("formatAgreement keeps two decimals, like coherence", "[readouts]") {
    CHECK(formatAgreement(0.7071) == "0.71");
    CHECK(formatAgreement(1.0) == "1.00");
    CHECK(formatAgreement(0.0) == "0.00");
}

TEST_CASE("formatContributors reads as a bare integer pair", "[readouts]") {
    CHECK(formatContributors(3, 4) == "3 of 4");
    CHECK(formatContributors(0, 4) == "0 of 4");
    CHECK(formatContributors(4, 4) == "4 of 4");
}
