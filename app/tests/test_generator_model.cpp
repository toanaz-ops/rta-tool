// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H7 task T1 (docs/plans/2026-10-21-h7-generator-output-plan.md): the
// generator's level policy and its one source constructor. Expectations are
// closed forms: a sine's peak is 10^(dB/20), a noise's RMS is its stated
// level -- never a value the implementation printed.

#include "measure/GeneratorModel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <variant>
#include <vector>

using namespace rta::measure;

namespace {

constexpr double kFs = 48000.0;

std::vector<float> render(rta::platform::SourceVariant source, std::size_t n) {
    std::vector<float> out(n, 0.0f);
    std::visit(
        [&](auto& s) {
            using T = std::decay_t<decltype(s)>;
            if constexpr (!std::is_same_v<T, std::monostate>) s.process(std::span<float>(out));
        },
        source);
    return out;
}

double rmsDb(const std::vector<float>& x) {
    double acc = 0.0;
    for (float v : x) acc += static_cast<double>(v) * v;
    return 10.0 * std::log10(acc / static_cast<double>(x.size()));
}

double peak(const std::vector<float>& x, std::size_t count) {
    double p = 0.0;
    for (std::size_t i = 0; i < count; ++i) p = std::max(p, std::abs(static_cast<double>(x[i])));
    return p;
}

}  // namespace

TEST_CASE("generator model: a sine's peak over one cycle is 10^(dB/20)") {
    GeneratorSettings s;
    s.signal = GeneratorSignal::Sine;
    s.frequencyHz = 1000.0;
    s.levelDb = -30.0;
    // 1000 Hz at 48 kHz is exactly 48 samples a cycle; sample 12 is sin(pi/2).
    const auto x = render(makeSource(s, kFs, 1), 48);
    CHECK(std::abs(peak(x, 48) - std::pow(10.0, -30.0 / 20.0)) < 1e-6);
}

TEST_CASE("generator model: the ceiling is enforced inside makeSource") {
    GeneratorSettings s;
    s.signal = GeneratorSignal::Sine;
    s.levelDb = 0.0;  // full scale requested
    const auto x = render(makeSource(s, kFs, 1), 48);
    CHECK(std::abs(peak(x, 48) - std::pow(10.0, kGeneratorCeilingDb / 20.0)) < 1e-6);

    s.levelDb = -200.0;  // far below the floor: plays the floor, not silence
    const auto y = render(makeSource(s, kFs, 1), 48);
    CHECK(std::abs(peak(y, 48) - std::pow(10.0, kGeneratorFloorDb / 20.0)) < 1e-6);
}

TEST_CASE("generator model: pink and white hit their RMS level within 0.2 dB over 2^18") {
    constexpr std::size_t kN = 1u << 18;
    for (const auto signal : {GeneratorSignal::Pink, GeneratorSignal::White}) {
        GeneratorSettings s;
        s.signal = signal;
        s.levelDb = -30.0;
        CHECK(std::abs(rmsDb(render(makeSource(s, kFs, 7), kN)) - (-30.0)) <= 0.2);
        s.levelDb = 0.0;  // clamped to the ceiling
        CHECK(std::abs(rmsDb(render(makeSource(s, kFs, 7), kN)) - kGeneratorCeilingDb) <= 0.2);
    }
}

TEST_CASE("generator model: a sine at an unusable sample rate builds without undefined behaviour") {
    // std::clamp(hz, 1, 0.45 * fs) is undefined for fs <= 2.2 (hi < lo).
    GeneratorSettings s;
    s.signal = GeneratorSignal::Sine;
    for (const double fs : {0.0, -48000.0, 1.0}) {
        CHECK_NOTHROW(makeSource(s, fs, 1));
    }
}

TEST_CASE("generator model: clamp and step") {
    CHECK(clampGeneratorLevelDb(0.0) == kGeneratorCeilingDb);
    CHECK(clampGeneratorLevelDb(-12.0) == -12.0);
    CHECK(clampGeneratorLevelDb(-59.9) == -59.9);
    CHECK(clampGeneratorLevelDb(-90.0) == kGeneratorFloorDb);
    CHECK(clampGeneratorLevelDb(std::nan("")) == kGeneratorFloorDb);
    CHECK(stepGeneratorLevelDb(-30.0, 1) == -29.0);
    CHECK(stepGeneratorLevelDb(-30.0, -1) == -31.0);
    CHECK(stepGeneratorLevelDb(-30.0, 20) == kGeneratorCeilingDb);   // 18 steps reach it, 20 stay
    CHECK(stepGeneratorLevelDb(-30.0, -40) == kGeneratorFloorDb);    // 30 steps reach it, 40 stay
    CHECK(kGeneratorDefaultLevelDb == -30.0);
}

TEST_CASE("generator model: texts follow the number-formatting rule") {
    GeneratorSettings s;
    CHECK(levelText(s) == "-30.0 dBFS RMS");  // noise: RMS, one decimal
    s.signal = GeneratorSignal::Sine;
    s.levelDb = -12.0;
    CHECK(levelText(s) == "-12.0 dBFS peak");  // sine: peak
    CHECK(frequencyText(1000.0) == "1000 Hz");  // whole hertz, no decimal
    CHECK(frequencyText(31.5) == "32 Hz");
    CHECK(signalName(GeneratorSignal::Pink) == "pink");
    CHECK(signalName(GeneratorSignal::White) == "white");
    CHECK(signalName(GeneratorSignal::Sine) == "sine");
    CHECK(outputsText({0}) == "out 1");
    CHECK(outputsText({0, 1}) == "out 1+2");
}

TEST_CASE("generator model: 31 whole-hertz sine frequencies from 20 Hz to 20 kHz") {
    CHECK(kSineFrequenciesHz.size() == 31);
    CHECK(kSineFrequenciesHz.front() == 20);
    CHECK(kSineFrequenciesHz.back() == 20000);
    CHECK(std::is_sorted(kSineFrequenciesHz.begin(), kSineFrequenciesHz.end()));
    CHECK(std::count(kSineFrequenciesHz.begin(), kSineFrequenciesHz.end(), 1000) == 1);
}

TEST_CASE("generator model: pruneOutputs") {
    CHECK(pruneOutputs({1, 0, 1}, 2) == std::vector<int>{0, 1});
    CHECK(pruneOutputs({0, 5}, 2) == std::vector<int>{0});
    CHECK(pruneOutputs({4, 5}, 2) == std::vector<int>{0});  // empty after pruning -> out 1
    CHECK(pruneOutputs({}, 2) == std::vector<int>{0});
    CHECK(pruneOutputs({3, -1}, 0) == std::vector<int>{3});  // unknown count: no upper limit
}
