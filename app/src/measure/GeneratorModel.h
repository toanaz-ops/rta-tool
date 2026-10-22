// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// enforced by the measure_has_no_framework_deps ctest.
//
// H7 task T1 (docs/plans/2026-10-21-h7-generator-output-plan.md, owner
// decision D2). The operator generator's vocabulary: which signal, how loud,
// on which outputs, and the ONE place a level request is turned into a
// source. Nothing here touches the engine; `GeneratorRunner` does that.
//
// Level policy (D2): default -30.0 dBFS, floor -60.0, hard ceiling -12.0,
// step 1.0 dB. The ceiling is the level at which pink noise's ~12 dB crest
// factor still just fits under full scale (gen/Noise.h), and it is enforced
// INSIDE `makeSource`, not by the caller, so no caller -- the rail, a
// measurement following the generator, a test -- can build a louder source
// by forgetting to clamp.
#pragma once

#include "rta/platform/OutputEngine.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rta::measure {

enum class GeneratorSignal { Pink, White, Sine };

inline constexpr double kGeneratorDefaultLevelDb = -30.0;
inline constexpr double kGeneratorFloorDb = -60.0;
inline constexpr double kGeneratorCeilingDb = -12.0;
inline constexpr double kGeneratorStepDb = 1.0;
inline constexpr double kGeneratorDefaultFrequencyHz = 1000.0;

/// The 31 ISO R10 nominal frequencies from 20 Hz to 20 kHz, whole hertz
/// (31.5 Hz is listed as 32, so every readout is a whole number of Hz).
inline constexpr std::array<int, 31> kSineFrequenciesHz = {
    20,   25,   32,   40,   50,   63,   80,   100,   125,   160,   200,
    250,  315,  400,  500,  630,  800,  1000, 1250,  1600,  2000,  2500,
    3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000};

struct GeneratorSettings {
    GeneratorSignal signal = GeneratorSignal::Pink;
    double levelDb = kGeneratorDefaultLevelDb;  ///< RMS for noise, peak for sine
    double frequencyHz = kGeneratorDefaultFrequencyHz;  ///< SINE only
    std::vector<int> outputs{0};  ///< zero-based device output channels, never empty
};

/// Clamps to [floor, ceiling]. NaN reads as the floor: the quietest answer is
/// the safe one when the request carries no number.
[[nodiscard]] double clampGeneratorLevelDb(double db) noexcept;

/// `current` moved by `steps` x 1.0 dB, then clamped.
[[nodiscard]] double stepGeneratorLevelDb(double current, int steps) noexcept;

/// The source for `settings` at `sampleRate`. The level is clamped HERE.
/// `seed` selects the noise stream (ignored by the sine).
[[nodiscard]] rta::platform::SourceVariant makeSource(const GeneratorSettings& settings,
                                                       double sampleRate, std::uint64_t seed);

/// Sorted, de-duplicated, and limited to [0, numOutputs). Empty after that
/// reads as output 0 (a generator always has somewhere to play).
/// `numOutputs <= 0` means "unknown" and only sorts and de-duplicates.
[[nodiscard]] std::vector<int> pruneOutputs(std::vector<int> outputs, int numOutputs);

/// "pink" / "white" / "sine".
[[nodiscard]] std::string signalName(GeneratorSignal signal);
/// "-30.0 dBFS RMS" (noise) or "-30.0 dBFS peak" (sine): one decimal.
[[nodiscard]] std::string levelText(const GeneratorSettings& settings);
/// "1000 Hz": whole hertz.
[[nodiscard]] std::string frequencyText(double hz);
/// "out 1", "out 1+2": one-based, as the operator counts connectors.
[[nodiscard]] std::string outputsText(const std::vector<int>& outputs);

}  // namespace rta::measure
