// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L-API Task D (record docs/dsp/2026-09-16-remote-api.md sec.6; sec.11
// items 1, 2, 5, 6). D1-D8 only -- Task E's spatial cases open their own
// file, per the plan's unconditional split.
//
// Every assertion here is a substring match or a byte-compare, and that is
// exactly as far as this file can see. Record sec.15 R16 is the finding: a
// stable-but-MALFORMED document passes all of it. Task F vendors a
// third-party parser and asserts what no substring match can reach.

#include <catch2/catch_test_macros.hpp>

#include "ApiFixture.h"

#include "api/ApiPolicy.h"
#include "api/ApiSerialise.h"
#include "api/ApiSettings.h"

#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>

using rta::api::ApiSettings;
using rta::api::clampPoints;
using rta::api::Request;
using rta::api::serialiseBands;
using rta::api::serialiseSpectrum;
using rta::api::serialiseStatus;
using rta::api::serialiseTransfer;
using rta::api::test::makeApiFixture;

namespace {

[[nodiscard]] std::size_t countOf(const std::string& haystack, const std::string& needle) {
    std::size_t count = 0;
    for (std::size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + needle.size())) {
        ++count;
    }
    return count;
}

[[nodiscard]] Request allPoints() {
    Request request;
    request.points = clampPoints(0, ApiSettings{});
    return request;
}

}  // namespace

TEST_CASE("D1 absent coherence is an ABSENT KEY, never null and never ones", "[api][serialise]") {
    // TransferBlock::coherence is an optional because a single frame gives
    // coherence identically 1.0 at every frequency, so a broken engine looks
    // perfect. That gate survives the wire or it was never a gate: a client
    // that sees no `coherence` key must draw no coherence.
    auto snapshot = makeApiFixture();
    snapshot.transfer->coherence.reset();
    const std::string body = serialiseTransfer(snapshot, allPoints());
    CHECK(body.find("coherence") == std::string::npos);
    CHECK(body.find("magnitudeDb") != std::string::npos);   // the rest still serves
}

TEST_CASE("D2 the axis kind is uniform and pointCount is MEASURED", "[api][serialise]") {
    const auto snapshot = makeApiFixture();
    const std::string body = serialiseTransfer(snapshot, allPoints());
    CHECK(body.find("\"kind\":\"uniform\"") != std::string::npos);
    // A uniform axis sends no frequency vector: bin i is at
    // i*sampleRate/fftSize and the client rebuilds it. That is the whole
    // difference from /mtw, which is why saying which kind costs one string.
    CHECK(body.find("frequencyHz") == std::string::npos);
    // Record sec.15 R11: pointCount is magnitudeDb.size(), NEVER fftSize/2+1.
    // TransferBlock carries no point count, and the two agree only when the
    // engine filled the whole half-spectrum.
    const auto expected = snapshot.transfer->magnitudeDb.size();
    CHECK(body.find("\"pointCount\":" + std::to_string(expected)) != std::string::npos);
}

TEST_CASE("D3 the schema version travels in the body, not only the path", "[api][serialise]") {
    // A body that is saved to a file or pasted into an issue has left the
    // URL behind, and with it the /api/v1/ segment.
    const auto snapshot = makeApiFixture();
    const ApiSettings settings{};
    CHECK(serialiseStatus(snapshot, settings).find("\"schemaVersion\":1") != std::string::npos);
    CHECK(serialiseTransfer(snapshot, allPoints()).find("\"schemaVersion\":1")
          != std::string::npos);
    CHECK(serialiseBands(snapshot).find("\"schemaVersion\":1") != std::string::npos);
    CHECK(serialiseSpectrum(snapshot, allPoints()).find("\"schemaVersion\":1")
          != std::string::npos);
}

TEST_CASE("D4 /status reports what this build actually serves", "[api][serialise]") {
    const auto snapshot = makeApiFixture();
    const std::string body = serialiseStatus(snapshot, ApiSettings{});
    // Six capability names (record sec.15 R12 -- six is the length of
    // `available`, not the endpoint count, which is eight).
    CHECK(body.find(
              "\"available\":[\"transfer\",\"mtw\",\"bands\",\"spectrum\",\"average\",\"positions\"]")
          != std::string::npos);
    // "spl" appears the day the Meters track puts SPL in the Snapshot and
    // not a day earlier: Snapshot carries dBFS only, and rta::meter::Leq has
    // no app/ caller. Claiming it here would be a capability list lying.
    CHECK(body.find("spl") == std::string::npos);
    CHECK(body.find("\"sequence\":12345") != std::string::npos);
}

TEST_CASE("D5 underResolved travels, per band", "[api][serialise]") {
    // The difference between a measurement and a band the FFT physically
    // cannot resolve. A client that drops it draws a fault that does not
    // exist, in exactly the place the engine was honest about.
    const auto snapshot = makeApiFixture();
    const std::string body = serialiseBands(snapshot);
    REQUIRE_FALSE(snapshot.bands.empty());
    CHECK(countOf(body, "\"underResolved\":") == snapshot.bands.size());
    CHECK(countOf(body, "\"centreHz\":") == snapshot.bands.size());
}

TEST_CASE("D6 the point cap bounds the output LENGTH", "[api][serialise]") {
    const ApiSettings settings{};
    auto snapshot = makeApiFixture();
    // Grow the spectrum past the cap so the clamp has something to do: the
    // fixture's own fftSize/2+1 is 2049, under 8192, and a cap tested only
    // against data that never reaches it is a cap nothing exercises
    // (memory/a-fixture-can-be-too-well-behaved-to-fail.md).
    snapshot.spectrumDb.assign(20'000, -42.5f);
    Request request;
    request.points = clampPoints(1'000'000, settings);
    REQUIRE(request.points == settings.maxPointsPerResponse);
    const std::string body = serialiseSpectrum(snapshot, request);
    CHECK(countOf(body, "-42.5") == static_cast<std::size_t>(settings.maxPointsPerResponse));
    CHECK(body.find("\"pointCount\":" + std::to_string(settings.maxPointsPerResponse))
          != std::string::npos);
}

TEST_CASE("D7 REGRESSION LOCK: the golden /snapshot body has not drifted", "[api][serialise]") {
    // This is a REGRESSION LOCK, not a correctness test. It proves the format
    // has not drifted; it proves nothing about whether any number in it is
    // right. Every number here is already owned by the test that proved it --
    // test_analyser_transfer.cpp, test_analyser_mtw.cpp, test_average_group.cpp,
    // test_synthetic_snapshot.cpp.
    //
    // To regenerate, name the hidden case below explicitly:
    //   rtatool_analysis_tests.exe "regenerate the API golden"
    // It is tagged [.] so Catch2 hides it, ctest never discovers it and no
    // wildcard run can reach it -- the shape
    // memory/a-gen-script-runs-the-moment-you-invoke-it.md asks for, where
    // regeneration takes an explicit act and nothing else overwrites the file.
    const auto snapshot = makeApiFixture();
    const std::string emitted = rta::api::serialiseSnapshot(snapshot, allPoints());

    std::ifstream file(std::string(RTA_API_GOLDEN_DIR) + "/api-v1-snapshot.json",
                       std::ios::binary);
    REQUIRE(file.good());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    CHECK(emitted == buffer.str());
}

TEST_CASE("D8 the golden is UTF-8 with LF endings and no BOM", "[api][serialise]") {
    // A CRLF or a BOM committed by a Windows editor would make the byte
    // compare above fail on ubuntu and macos only -- a lock that holds on one
    // of the three CI operating systems is not a lock.
    std::ifstream file(std::string(RTA_API_GOLDEN_DIR) + "/api-v1-snapshot.json",
                       std::ios::binary);
    REQUIRE(file.good());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    CHECK(text.find('\r') == std::string::npos);
    CHECK(text.rfind("\xEF\xBB\xBF", 0) != 0);
    CHECK_FALSE(text.empty());
}

TEST_CASE("regenerate the API golden", "[.][api][golden-write]") {
    // Hidden ([.]): Catch2 excludes it from --list-tests, so
    // catch_discover_tests never registers it and no ctest run and no
    // wildcard filter can trip it. It overwrites the lock, so reaching it
    // must be an explicit act -- name it on the command line or it does not
    // run. std::ios::binary keeps the LF endings D8 asserts on Windows.
    const auto snapshot = makeApiFixture();
    std::ofstream out(std::string(RTA_API_GOLDEN_DIR) + "/api-v1-snapshot.json",
                      std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    out << rta::api::serialiseSnapshot(snapshot, allPoints());
    CHECK(out.good());
}
