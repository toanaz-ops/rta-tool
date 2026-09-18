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
#include <cstdlib>
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

TEST_CASE("D6b a DEFAULT-CONSTRUCTED Request is capped, not unbounded", "[api][serialise]") {
    // The cap is a REAL-TIME-SAFETY control (record sec.9), and until this
    // case existed it depended on every caller remembering to run
    // clampPoints. `Request{}` meant "no limit", so a wave-2 handler that
    // built a Request without going through clampPoints would have served an
    // unbounded body -- the control gone, with nothing red to say so.
    //
    // A default now means the SHIPPED cap, which is the conservative reading:
    // an operator who configured a LARGER cap and forgot to clamp gets 8192
    // rather than everything.
    const ApiSettings settings{};
    auto snapshot = makeApiFixture();
    snapshot.spectrumDb.assign(20'000, -42.5f);

    const Request defaulted{};
    CHECK(defaulted.points == settings.maxPointsPerResponse);

    const std::string body = serialiseSpectrum(snapshot, defaulted);
    CHECK(countOf(body, "-42.5") == static_cast<std::size_t>(settings.maxPointsPerResponse));
    CHECK(body.find("\"pointCount\":" + std::to_string(settings.maxPointsPerResponse))
          != std::string::npos);

    // And the same through the union endpoint, which is the one a client
    // reaches for when it wants everything at once. Asserted on the emitted
    // pointCount rather than by counting "-42.5" occurrences: the union body
    // also carries transfer.phaseDeg[482] == -42.539062, whose decimal
    // CONTAINS that substring, so a substring count reads 8193 here and the
    // test would be measuring its own sloppiness rather than the cap.
    const std::string unionBody = rta::api::serialiseSnapshot(snapshot, defaulted);
    CHECK(unionBody.find("\"pointCount\":" + std::to_string(settings.maxPointsPerResponse))
          != std::string::npos);
    CHECK(unionBody.find("\"pointCount\":20000") == std::string::npos);
}

TEST_CASE("D7 REGRESSION LOCK: the golden /snapshot body has not drifted", "[api][serialise]") {
    // This is a REGRESSION LOCK, not a correctness test. It proves the format
    // has not drifted; it proves nothing about whether any number in it is
    // right. Every number here is already owned by the test that proved it --
    // test_analyser_transfer.cpp, test_analyser_mtw.cpp, test_average_group.cpp,
    // test_synthetic_snapshot.cpp.
    //
    // To regenerate, BOTH halves are required and the environment variable
    // is the half that actually holds:
    //   RTA_API_GOLDEN_WRITE=1 rtatool_analysis_tests.exe "regenerate the API golden"
    // The `[.]` tag alone does NOT protect this file -- it hides the
    // regenerator from the DEFAULT run only, and a filtered run such as
    // `exe "[api]"` used to reach it and rewrite the lock mid-suite. See the
    // regenerator's own comment for the measurement, and D9 for the gate.
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

/// True only when the environment says so. This is the gate on the
/// regenerator, and the reason it is an ENVIRONMENT VARIABLE rather than a
/// Catch2 tag is a measured defect, not a preference:
///
/// `[.]` hides a case from the DEFAULT run only. It does not hide it from a
/// filtered one. Measured on this exact binary: with the golden replaced by
/// an 8-byte sentinel, `rtatool_analysis_tests.exe "[api]"` ran the
/// regenerator, **rewrote the golden to 198045 bytes**, and a second
/// identical invocation then PASSED. A real format regression would have
/// reported itself once and then repaired the evidence -- the regression lock
/// silently unlocking itself, which is worse than having no lock, because the
/// green on run two looks like proof.
///
/// A tag cannot fix this, because the attack surface IS the tag matcher. The
/// gate has to be something no `-# [tag]` or `[*]` expression can supply.
[[nodiscard]] bool goldenWriteRequested() {
#if defined(_MSC_VER)
    // std::getenv is deprecated under MSVC's secure-CRT warnings, and this
    // target builds with /W4 as 0-warnings.
    std::size_t length = 0;
    char value[8] = {};
    if (getenv_s(&length, value, sizeof value, "RTA_API_GOLDEN_WRITE") != 0) {
        return false;
    }
    return length != 0 && value[0] == '1';
#else
    const char* const value = std::getenv("RTA_API_GOLDEN_WRITE");
    return value != nullptr && value[0] == '1' && value[1] == '\0';
#endif
}

TEST_CASE("regenerate the API golden", "[.][golden-write]") {
    // Two independent gates, and the SECOND is the one that actually holds.
    //
    //  1. `[.]` keeps it out of the default run and out of
    //     catch_discover_tests, so ctest never registers it. Necessary, and
    //     NOT sufficient -- see goldenWriteRequested above for the measurement.
    //  2. RTA_API_GOLDEN_WRITE=1 must be in the environment. A test filter
    //     cannot set an environment variable, so no invocation of this binary
    //     that merely SELECTS this case can make it write.
    //
    // Its tag list also no longer carries `[api]`: the lane's own tag was the
    // one that reached it, and a case that rewrites a lock has no business
    // answering to the tag every test in the lane shares.
    //
    // To regenerate, both halves, deliberately:
    //   RTA_API_GOLDEN_WRITE=1 rtatool_analysis_tests.exe "regenerate the API golden"
    //
    // memory/a-gen-script-runs-the-moment-you-invoke-it.md is the lesson this
    // is the second attempt at honouring.
    if (!goldenWriteRequested()) {
        SKIP("RTA_API_GOLDEN_WRITE=1 is not set, so the golden is left alone. "
             "Selecting this case is not the same as asking for a rewrite.");
    }

    const auto snapshot = makeApiFixture();
    // std::ios::binary keeps the LF endings D8 asserts, on Windows too.
    std::ofstream out(std::string(RTA_API_GOLDEN_DIR) + "/api-v1-snapshot.json",
                      std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    out << rta::api::serialiseSnapshot(snapshot, allPoints());
    CHECK(out.good());
}

TEST_CASE("D9 a filtered run cannot rewrite the golden", "[api][serialise]") {
    // The defect above, as a test rather than as a comment. This case runs
    // under the very filter that used to trip the regenerator -- `[api]` --
    // and asserts that the gate is shut while it does so.
    //
    // It cannot observe the file directly without racing the case it is
    // testing, so it asserts the GATE, which is the thing that was missing.
    // The end-to-end proof (sentinel byte-for-byte survives `exe "[api]"`)
    // is in the commit message, where a two-process experiment belongs.
    CHECK_FALSE(goldenWriteRequested());
}
