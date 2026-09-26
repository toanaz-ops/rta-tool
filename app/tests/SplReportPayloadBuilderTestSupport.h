// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Shared fixtures for test_spl_report_payload_builder.cpp and
// test_spl_report_payload_builder_fixes.cpp (task W2-E2b fix round split,
// the SplReportTestSupport.h / CodeLines.h precedent: one fixture set shared
// by more than one test file, `inline` so it is not an ODR violation to
// include this from both).
#pragma once

#include "export/SplLog.h"
#include "measure/SplConfig.h"

#include "rta/meter/Block.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rta::splexport::test {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char* name)
        : path(std::filesystem::temp_directory_path() / "rta-test-splreportpayload" / name) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

/// blockSamples fixed at 480 for every block, matching the other SPL log
/// fixtures in this suite (test_spl_log_pipeline.cpp's own `distinguishableBlock`).
inline rta::meter::Block block(std::uint64_t index, double sumSquares) {
    rta::meter::Block b;
    b.blockIndex = index;
    b.blockSamples = 480;
    b.sumSquares = sumSquares;
    return b;
}

/// Writes one channel's log directly through `rta::splexport::SplLogWriter`
/// -- the SAME writer `SplLogPipeline`'s writer thread uses in production --
/// with `referenceOffsetDb` stamped into the header exactly as
/// `AnalysisThreadSpl.cpp` does. `segmentBlocks` large enough that this
/// fixture's handful of blocks never rotates, so there is exactly one
/// `ch<channel>.gen0.seg0.csv` for the payload builder to find.
inline void writeChannelLog(const std::filesystem::path& dir, int channel,
                            const std::vector<rta::meter::Block>& blocks,
                            double referenceOffsetDb) {
    rta::measure::SplConfig config;
    config.referenceOffsetDb = referenceOffsetDb;
    config.calibrated = referenceOffsetDb != 0.0;

    SplLogHeaderInfo info;
    info.weighting = rta::dsp::WeightingType::A;
    info.detector = rta::meter::TimeWeighting::Fast;
    info.blockSamples = 480;
    info.sampleRate = 48000.0;
    info.startedAtUnixMs = 1'700'000'000'000ull;

    SplLogWriter writer((dir / ("ch" + std::to_string(channel))).string(), config, info,
                        /*segmentBlocks=*/1000);
    for (const auto& b : blocks) writer.write(b);
    // The destructor closes `stream_`, flushing every write above to disk --
    // the same lifetime the writer thread's own SplLogWriter has when
    // SplLogPipeline::disable() joins it.
}

/// Four quiet blocks (raw mean-square 1.0, i.e. 0 dB before any offset) and
/// four LOUD blocks (raw mean-square 1e6, 60 dB above quiet) -- the "loud
/// invalid span" the task's own self-check asks for: excluding blocks
/// 3..6 must move the whole-session Leq by tens of dB, not a rounding
/// difference.
inline std::vector<rta::meter::Block> eightBlockFixture() {
    std::vector<rta::meter::Block> blocks;
    blocks.push_back(block(0, 480.0 * 1.0));
    blocks.push_back(block(1, 480.0 * 1.0));
    blocks.push_back(block(2, 480.0 * 1.0));
    blocks.push_back(block(3, 480.0 * 1.0e6));
    blocks.push_back(block(4, 480.0 * 1.0e6));
    blocks.push_back(block(5, 480.0 * 1.0e6));
    blocks.push_back(block(6, 480.0 * 1.0e6));
    blocks.push_back(block(7, 480.0 * 1.0));
    return blocks;
}

}  // namespace rta::splexport::test
