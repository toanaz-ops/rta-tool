// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H7 task T13 (docs/plans/2026-10-21-h7-generator-output-plan.md): VERIFY and
// ALIGN against a REAL OutputEngine rendered by hand, with a host that
// supplies a measurement drive. What reaches the loudspeaker is read off the
// engine's own output: RMS -30 dBFS where the drive says -30, VERIFY on the
// generator's outputs, ALIGN on its own main/sub whatever the drive says.
// Without a drive both keep their pre-H7 words and -12 dBFS (the existing
// runner suites pin that and are unchanged).

#include "AlignRunnerFixture.h"
#include "EqVerifyRunnerFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using rta::measure::MeasurementDrive;
using rta::platform::OutputRole;

namespace {

struct Heard {
    double rmsDb0 = 0.0;
    float peak1 = 0.0f;
};

/// Renders `blocks` blocks and reads output 0's RMS (skipping the gate ramp)
/// and output 1's peak. `block` and `channels` are the fixture's.
template <class Engine>
Heard listen(Engine& engine, int blocks, int block, int channels) {
    Heard heard;
    double acc = 0.0;
    std::size_t counted = 0;
    for (int b = 0; b < blocks; ++b) {
        std::vector<std::vector<float>> buffers(static_cast<std::size_t>(channels),
                                                std::vector<float>(static_cast<std::size_t>(block), 0.0f));
        std::vector<float*> ptrs;
        for (auto& channel : buffers) ptrs.push_back(channel.data());
        engine.render(ptrs.data(), channels, block);
        if (b < 8) continue;
        for (const float s : buffers[0]) { acc += static_cast<double>(s) * s; ++counted; }
        for (const float s : buffers[1]) heard.peak1 = std::max(heard.peak1, std::abs(s));
    }
    heard.rmsDb0 = 10.0 * std::log10(acc / static_cast<double>(counted));
    return heard;
}

}  // namespace

TEST_CASE("T13 VERIFY: plays at the drive's level on the drive's outputs and says so") {
    using verifyfixture::Rig;
    Rig rig(true, true, MeasurementDrive{-30.0, {0, 1}});
    REQUIRE(rig.runner.press());
    CHECK(rig.engine.role(0) == OutputRole::Routed);
    CHECK(rig.engine.role(1) == OutputRole::Routed);
    CHECK(rig.model.status().find("@ -30.0 dBFS RMS, out 1+2") != std::string::npos);
    const Heard heard = listen(rig.engine, 400, verifyfixture::kBlock, verifyfixture::kChannels);
    INFO("measured RMS " << heard.rmsDb0 << " dBFS");
    CHECK(std::abs(heard.rmsDb0 - (-30.0)) < 1.0);  // not -12: the drive's level
    CHECK(heard.peak1 > 0.0f);                       // out 2 is on too
}

TEST_CASE("T13 VERIFY: the drive's outputs replace output 1, and the stored trace names level and output") {
    using verifyfixture::Rig;
    Rig rig(true, true, MeasurementDrive{-30.0, {1}});
    REQUIRE(rig.runToDone());
    CHECK(rig.engine.role(0) == OutputRole::None);  // out 1 is NOT played
    CHECK(rig.engine.role(1) == OutputRole::Routed);
    CHECK(rig.model.status().find(" @ -30.0 dBFS RMS, out 2") != std::string::npos);
    const auto* entry = rig.library.entry(rig.runner.afterTraceIdForTest());
    REQUIRE(entry != nullptr);
    CHECK(entry->name == "VERIFY @ 12:34:56 (-30.0 dBFS RMS, out 2)");
}

TEST_CASE("T13 VERIFY: an empty output list keeps output 1, and the hint names the level") {
    using verifyfixture::Rig;
    Rig rig(true, true, MeasurementDrive{-45.0, {}});
    REQUIRE(rig.runner.press());
    CHECK(rig.engine.role(0) == OutputRole::Routed);
    CHECK(rig.engine.role(1) == OutputRole::None);
    CHECK(rig.model.status().find("@ -45.0 dBFS RMS, out 1") != std::string::npos);
    CHECK(rig.runner.driveHint() == " -- plays at -45.0 dBFS RMS");
    rig.driveValue.levelDb = -20.0;  // the hint reads the NEXT press's level
    CHECK(rig.runner.driveHint() == " -- plays at -20.0 dBFS RMS");

    Rig legacy;  // no drive: no hint, the old words
    CHECK(legacy.runner.driveHint().empty());
}

TEST_CASE("T13 ALIGN: the level follows the drive, the outputs stay main and sub", "[align_runner]") {
    using alignfixture::Rig;
    Rig rig(MeasurementDrive{-30.0, {1}});  // GEN ticked out 2 only; ALIGN must not care
    rig.answerAll();
    REQUIRE(rig.runner.press());
    CHECK(rig.engine.role(0) == OutputRole::Routed);  // MAIN (out 1), solo
    CHECK(rig.engine.role(1) == OutputRole::None);
    CHECK(rig.runner.status().find("@ -30.0 dBFS RMS") != std::string::npos);
    const Heard heard = listen(rig.engine, 100, alignfixture::kBlock, alignfixture::kChannels);
    INFO("measured RMS " << heard.rmsDb0 << " dBFS");
    CHECK(std::abs(heard.rmsDb0 - (-30.0)) < 1.0);
    CHECK(heard.peak1 == 0.0f);  // SUB is silent while MAIN is measured
}

TEST_CASE("T13 ALIGN: every status after the arm and every stored name carries the level", "[align_runner]") {
    using alignfixture::Rig;
    Rig rig(MeasurementDrive{-30.0, {}});
    rig.answerAll();
    REQUIRE(rig.runToVerdict());
    CHECK(rig.runner.status().find(" @ -30.0 dBFS RMS") != std::string::npos);
    REQUIRE(rig.library.entries().size() == 2);
    CHECK(rig.library.entries()[0].name == "ALIGN MAIN @ 12:34:56 (-30.0 dBFS RMS)");
    CHECK(rig.library.entries()[1].name == "ALIGN SUB @ 12:34:56 (-30.0 dBFS RMS)");

    REQUIRE(rig.runner.answerCycleMs(2.0));
    rig.driveValue.levelDb = -24.0;  // the operator moved GEN between MEASURE and SUM
    REQUIRE(rig.runner.pressSum());
    CHECK(rig.runner.status().find("@ -24.0 dBFS RMS") != std::string::npos);  // SUM names ITS level
    REQUIRE(rig.pumpUntil([&] { return !rig.runner.busy(); }, 400));
    CHECK(rig.library.entries().back().name == "ALIGN SUM @ 12:34:56 (-24.0 dBFS RMS)");
    CHECK(rig.runner.driveHint() == " -- plays at -24.0 dBFS RMS");
}

TEST_CASE("T13 ALIGN: without a drive the level is the record's -12 and the words are unchanged",
          "[align_runner]") {
    using alignfixture::Rig;
    Rig rig;
    rig.answerAll();
    REQUIRE(rig.runner.press());
    CHECK(rig.runner.status().find('@') == std::string::npos);
    const Heard heard = listen(rig.engine, 100, alignfixture::kBlock, alignfixture::kChannels);
    CHECK(std::abs(heard.rmsDb0 - rta::measure::kAlignLevelDbFs) < 1.0);
    CHECK(rig.runner.driveHint().empty());
}
