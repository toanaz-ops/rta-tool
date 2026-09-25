// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a Wave 2, task W2-C (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md §10, §13 Q7).
#include "export/SplLog.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>

using namespace rta::splexport;
using rta::measure::SplConfig;
using rta::meter::Block;

namespace {

/// A directory that removes itself, so a failing assertion cannot leave the
/// next run reading a previous run's log (test_session_store.cpp's own
/// convention).
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char* name)
        : path(std::filesystem::temp_directory_path() / "rta-test-spllog" / name) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

Block blockAtLevel(std::uint64_t index, std::uint32_t samples, double levelDb) {
    Block b;
    b.blockIndex = index;
    b.blockSamples = samples;
    b.sumSquares = static_cast<double>(samples) * std::pow(10.0, levelDb / 10.0);
    return b;
}

std::string readWholeFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

SplLogHeaderInfo headerInfo() {
    SplLogHeaderInfo info;
    info.weighting = rta::dsp::WeightingType::A;
    info.detector = rta::meter::TimeWeighting::Fast;
    info.blockSamples = 48000;
    info.sampleRate = 48000.0;
    info.startedAtUnixMs = 1'700'000'000'000ull;
    return info;
}

}  // namespace

// --- C1: the header is one key=value pair per line --------------------------

TEST_CASE("C1 logHeader is one pair per line with the exact spelled keys", "[spl_log]") {
    SplConfig config;
    config.referenceOffsetDb = 12.5;
    config.calibrated = true;
    auto info = headerInfo();
    info.calibratorLevelDb = 94.0;

    const std::string header = logHeader(config, info);

    for (const char* key :
        { "schema=", "startedAtUnixMs=", "sampleRate=", "blockSamples=", "weighting=", "detector=",
          "calibrationOffsetDb=", "calibrationUnit=", "calibratorLevelDb=", "histogramBaseDb=" }) {
        INFO("missing key " << key);
        CHECK(header.find(key) != std::string::npos);
    }

    // Every non-empty line starts with "# " -- one pair per line (SPL-R6).
    std::istringstream stream(header);
    std::string line;
    std::size_t lineCount = 0;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        ++lineCount;
        CHECK(line.rfind("# ", 0) == 0);
        // Exactly one '=' worth of "key=value" shape per line (beyond the
        // "# " prefix) -- multiple pairs never share a line.
        CHECK(line.find('=') != std::string::npos);
    }
    CHECK(lineCount == 10);
}

TEST_CASE("C1 calibratorLevelDb is absent from the header when uncalibrated", "[spl_log]") {
    SplConfig config;
    const std::string header = logHeader(config, headerInfo());
    CHECK(header.find("calibratorLevelDb=") == std::string::npos);
}

// --- C2: sumSquares round-trips bit-exactly ---------------------------------

TEST_CASE("C2 sumSquares round-trips bitwise through 1000 written and read blocks", "[spl_log]") {
    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> dist(30.0, 130.0);

    std::string body;
    std::vector<Block> written;
    for (std::uint64_t i = 0; i < 1000; ++i) {
        Block b = blockAtLevel(i, 480, dist(rng));
        written.push_back(b);
        body += logRow(b, 0.0);
    }

    const auto result = readLog(body);
    REQUIRE(result.blocks.size() == 1000);
    CHECK(result.bytesDiscarded == 0);
    for (std::size_t i = 0; i < written.size(); ++i) {
        INFO("row " << i);
        CHECK(result.blocks[i].sumSquares == written[i].sumSquares);  // BITWISE
        CHECK(result.blocks[i].blockIndex == written[i].blockIndex);
        CHECK(result.blocks[i].blockSamples == written[i].blockSamples);
    }
}

// --- C3: an interrupted append costs at most one line -----------------------

TEST_CASE("C3 readLog drops only the truncated final line and reports its length", "[spl_log]") {
    std::string body;
    for (std::uint64_t i = 0; i < 20; ++i) body += logRow(blockAtLevel(i, 480, 90.0), 0.0);

    const auto fullResult = readLog(body);
    REQUIRE(fullResult.blocks.size() == 20);
    CHECK(fullResult.bytesDiscarded == 0);

    // Truncate at 20 arbitrary offsets INSIDE the last row.
    const std::size_t lastRowStart = body.rfind('\n', body.size() - 2) + 1;
    const std::string lastRow = body.substr(lastRowStart);
    for (int cut = 1; cut <= 20; ++cut) {
        const std::size_t cutLen = (lastRow.size() * static_cast<std::size_t>(cut)) / 21;
        if (cutLen == 0 || cutLen >= lastRow.size() - 1) continue;  // stay inside the row
        const std::string truncated = body.substr(0, lastRowStart + cutLen);
        const auto result = readLog(truncated);
        INFO("cut " << cut << " cutLen " << cutLen);
        REQUIRE(result.blocks.size() == 19);  // every COMPLETE row still reads
        CHECK(result.bytesDiscarded == truncated.size() - lastRowStart);
    }
}

// --- C4: settings cannot change mid-log -------------------------------------

TEST_CASE("C4 reconfigure starts a brand new log rather than a mixed file", "[spl_log]") {
    TempDir dir("c4");
    SplConfig config;
    SplLogWriter writer((dir.path / "channel").string(), config, headerInfo(), 3600);

    writer.write(blockAtLevel(0, 48000, 80.0));
    REQUIRE(writer.segmentPaths().size() == 1);
    const auto firstPath = writer.segmentPaths().front();
    const std::string firstHeader = readWholeFile(firstPath);
    CHECK(firstHeader.find("weighting=A") != std::string::npos);

    // A no-op reconfigure (same values) does NOT start a new log.
    writer.reconfigure(rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast);
    CHECK(writer.segmentPaths().size() == 1);

    // An ACTUAL change starts a brand new file -- never appended to the one
    // above, which is what "no per-row weighting/detector column" requires:
    // there is nowhere else for the new value to be recorded.
    writer.reconfigure(rta::dsp::WeightingType::C, rta::meter::TimeWeighting::Slow);
    REQUIRE(writer.segmentPaths().size() == 2);
    CHECK(writer.segmentPaths()[1] != firstPath);
    writer.write(blockAtLevel(1, 48000, 80.0));

    const std::string secondHeader = readWholeFile(writer.segmentPaths()[1]);
    CHECK(secondHeader.find("weighting=C") != std::string::npos);
    CHECK(secondHeader.find("detector=Slow") != std::string::npos);
    // The FIRST file is untouched -- still says A/Fast, never overwritten
    // with a mixed row.
    CHECK(readWholeFile(firstPath).find("weighting=A") != std::string::npos);

    // Structural: no per-row column carries weighting or detector -- the CSV
    // header names exactly nine numeric columns, none of them either word.
    CHECK(std::string(csvHeaderRow()).find("weighting") == std::string::npos);
    CHECK(std::string(csvHeaderRow()).find("detector") == std::string::npos);
}

// --- C5: rotation is by a declared block count ------------------------------

TEST_CASE("C5 the writer opens segment 1 at block 3600 and never deletes", "[spl_log]") {
    TempDir dir("c5");
    SplConfig config;
    SplLogWriter writer((dir.path / "channel").string(), config, headerInfo(), 3600);

    for (std::uint64_t i = 0; i < 3600; ++i) writer.write(blockAtLevel(i, 48000, 80.0));
    REQUIRE(writer.segmentPaths().size() == 1);

    writer.write(blockAtLevel(3600, 48000, 80.0));  // the 3601st block
    REQUIRE(writer.segmentPaths().size() == 2);
    for (const auto& path : writer.segmentPaths()) CHECK(std::filesystem::exists(path));

    // Structural: SplLogWriter.cpp names no deletion call.
    const std::string source = readWholeFile(
        std::filesystem::path(RTA_REPO_ROOT) / "app" / "src" / "export" / "SplLogWriter.cpp");
    for (const char* needle : { "std::remove(", "std::filesystem::remove", "unlink(", "DeleteFile" }) {
        CHECK(source.find(needle) == std::string::npos);
    }
}

// --- C6: one file per logged channel ----------------------------------------

TEST_CASE("C6 a two-channel session writes two files, not one wide CSV", "[spl_log]") {
    TempDir dir("c6");
    SplConfig config;
    SplLogWriter left((dir.path / "left").string(), config, headerInfo(), 3600);
    SplLogWriter right((dir.path / "right").string(), config, headerInfo(), 3600);

    left.write(blockAtLevel(0, 48000, 80.0));
    right.write(blockAtLevel(0, 48000, 85.0));

    CHECK(left.segmentPaths().front() != right.segmentPaths().front());
    // Each row is exactly the nine-column shape -- never widened by a second
    // channel's columns appended to the same line.
    const std::string leftCsv = readWholeFile(left.segmentPaths().front());
    const auto lastLine = leftCsv.substr(leftCsv.find(csvHeaderRow()) + csvHeaderRow().size() + 1);
    CHECK(std::count(lastLine.begin(), lastLine.end(), ',') == 8);  // 9 columns, 8 commas
}

// --- C7: JSON is the same schema, not a second one --------------------------

TEST_CASE("C7 logJson's key set equals the CSV header's column set", "[spl_log]") {
    const Block block = blockAtLevel(5, 480, 97.3);
    const std::string json = logJson(block, 0.0);

    // Every value in this JSON is a bare number, so every quoted substring is
    // a KEY -- no value-skipping logic is needed to tell the two apart.
    std::set<std::string> jsonKeys;
    std::size_t pos = 0;
    while (true) {
        const auto start = json.find('"', pos);
        if (start == std::string::npos) break;
        const auto end = json.find('"', start + 1);
        if (end == std::string::npos) break;
        jsonKeys.insert(json.substr(start + 1, end - start - 1));
        pos = end + 1;
    }

    std::set<std::string> csvKeys;
    std::istringstream columns{ std::string(csvHeaderRow()) };
    std::string column;
    while (std::getline(columns, column, ',')) csvKeys.insert(column);

    CHECK(jsonKeys == csvKeys);
}

// --- C8: the row carries droppedSamples; time is reconstructible -----------

TEST_CASE("C8 the header names droppedSamples and reconstructs sample position", "[spl_log]") {
    CHECK(std::string(csvHeaderRow()) ==
         "blockIndex,blockSamples,droppedSamples,sumSquares,leqDb,maxFastDb,maxSlowDb,peakCDb,flags");

    std::vector<Block> blocks;
    for (std::uint64_t i = 0; i < 6; ++i) {
        Block b = blockAtLevel(i, 480, 90.0);
        if (i == 4) b.droppedSamples = 12000;  // a 12000-sample drop before block 4
        blocks.push_back(b);
    }
    std::string body;
    for (const auto& b : blocks) body += logRow(b, 0.0);
    const auto result = readLog(body);
    REQUIRE(result.blocks.size() == 6);

    std::uint64_t reconstructedPosition = 0;
    for (std::size_t i = 0; i < result.blocks.size(); ++i) {
        // A reader holding ONLY the text: sigma(blockSamples + droppedSamples).
        reconstructedPosition += result.blocks[i].blockSamples + result.blocks[i].droppedSamples;
    }
    CHECK(reconstructedPosition == 6ull * 480 + 12000ull);
}

// --- C9: t_iso is a reconstructed elapsed time ------------------------------

TEST_CASE("C9 reconstructedElapsedUnixMs is early-immune to a gap", "[spl_log]") {
    const std::uint64_t startedAt = 1'700'000'000'000ull;
    const double fs = 48000.0;

    // Without the drop: 4 blocks of 48000 samples is exactly 4 seconds.
    const auto noGap = reconstructedElapsedUnixMs(startedAt, fs, 4ull * 48000ull);
    CHECK(noGap == startedAt + 4000);

    // WITH a 12000-sample drop folded into the running sum (the "read off
    // only the text" reconstruction from C8): the elapsed count is larger,
    // so the reconstructed instant is LATER than the naive
    // blockIndex*blockSamples/fs formula would have said -- it is not
    // silently early for the rest of the session (SPL-R2, as amended).
    const auto withGap = reconstructedElapsedUnixMs(startedAt, fs, 4ull * 48000ull + 12000ull);
    CHECK(withGap > noGap);
    CHECK(withGap == startedAt + 4000 + 250);  // 12000/48000 s = 250 ms
}
