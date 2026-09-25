// SPDX-License-Identifier: AGPL-3.0-or-later
// PR #26 fix round, item 7 (record §10, deviation 5 from the original W2-C
// PR body): "one file per logged channel plus one session header".
#include "export/SplSessionHeader.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace rta::splexport;

namespace {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char* name)
        : path(std::filesystem::temp_directory_path() / "rta-test-spllog" / name) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

std::string readWholeFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

TEST_CASE("sessionHeader is one key=value pair per line naming every channel file",
         "[spl_session_header]") {
    SplSessionHeaderInfo info;
    info.startedAtUnixMs = 1'700'000'000'000ull;
    info.sampleRate = 48000.0;
    info.blockSamples = 48000;
    info.channelFiles = { "left.gen0.seg0.csv", "right.gen0.seg0.csv" };

    const std::string header = sessionHeader(info);

    for (const char* key : { "schema=", "startedAtUnixMs=", "sampleRate=", "blockSamples=",
                             "channelCount=", "channelFile0=", "channelFile1=" }) {
        INFO("missing key " << key);
        CHECK(header.find(key) != std::string::npos);
    }
    CHECK(header.find("channelFile0=left.gen0.seg0.csv") != std::string::npos);
    CHECK(header.find("channelFile1=right.gen0.seg0.csv") != std::string::npos);
    CHECK(header.find("channelCount=2") != std::string::npos);

    // One pair per line -- SPL-R6's own convention, never several on one.
    std::istringstream stream(header);
    std::string line;
    std::size_t lineCount = 0;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        ++lineCount;
        CHECK(line.rfind("# ", 0) == 0);
    }
    CHECK(lineCount == 7);  // schema, startedAtUnixMs, sampleRate, blockSamples, channelCount, x2 files
}

TEST_CASE("sessionHeader lists no channels when none are open yet", "[spl_session_header]") {
    SplSessionHeaderInfo info;
    info.startedAtUnixMs = 0;
    info.sampleRate = 48000.0;
    info.blockSamples = 48000;
    const std::string header = sessionHeader(info);
    CHECK(header.find("channelCount=0") != std::string::npos);
    CHECK(header.find("channelFile0=") == std::string::npos);
}

TEST_CASE("writeSessionHeaderFile writes exactly sessionHeader's own text to disk",
         "[spl_session_header]") {
    TempDir dir("session-header");
    SplSessionHeaderInfo info;
    info.startedAtUnixMs = 1'700'000'000'000ull;
    info.sampleRate = 48000.0;
    info.blockSamples = 48000;
    info.channelFiles = { "left.gen0.seg0.csv", "right.gen0.seg0.csv" };

    const auto path = (dir.path / "session.spllog").string();
    writeSessionHeaderFile(path, info);

    REQUIRE(std::filesystem::exists(path));
    CHECK(readWholeFile(path) == sessionHeader(info));
}
