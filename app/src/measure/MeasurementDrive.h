// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// enforced by the measure_has_no_framework_deps ctest.
//
// H7 task T13 (docs/plans/2026-10-21-h7-generator-output-plan.md, owner
// decision D1). ONE place answers "what level, and on which outputs, does a
// measurement play?" -- LOCATE, VERIFY, SWEEP (rail, ALIGN and RT60 presses)
// and ALIGN all ask `measurementDrive`, so the mode toggle of wave B2 changes
// one function, not five call sites.
//
// FOLLOW GEN (the default mode): the measurement plays at the generator's
// current level SETTING -- the generator need not be playing -- in the
// measurement's own convention (noise as dBFS RMS, a sweep as dBFS peak), and
// on the generator's selected outputs. The ceiling is the same -12 dBFS that
// bounds the generator (`clampGeneratorLevelDb`), so a measurement is never
// louder than before H7.
//
// OWN: the record-designed -12 dBFS, and an empty output list, which every
// caller reads as "keep the output this measurement chooses by design" (ALIGN
// solo main/sub, RT60's pane-chosen output, LOCATE/VERIFY/SWEEP's out 1).
#pragma once

#include "measure/GeneratorModel.h"

#include <string>
#include <vector>

namespace rta::measure {

enum class MeasurementLevelMode { FollowGen, Own };

/// What every measurement played at before H7 (LOCATE, VERIFY, SWEEP, ALIGN).
inline constexpr double kOwnMeasurementLevelDb = -12.0;

/// How a level number is read: noise is calibrated by RMS, a sweep by its peak.
enum class LevelConvention { Rms, Peak };

struct MeasurementDrive {
    double levelDb = kOwnMeasurementLevelDb;
    /// Zero-based device outputs; EMPTY = this measurement keeps its own output.
    std::vector<int> outputs;
};

/// The drive for `mode` given the generator's settings and the device's
/// output count (`numOutputs <= 0` = unknown). The level is clamped here.
[[nodiscard]] MeasurementDrive measurementDrive(MeasurementLevelMode mode, const GeneratorSettings& generator,
                                                int numOutputs);

/// "-30.0 dBFS RMS" / "-30.0 dBFS peak": one decimal, the convention named.
[[nodiscard]] std::string measurementLevelText(double levelDb, LevelConvention convention);
/// "-30.0 dBFS peak, out 1" -- or the level alone when `outputs` is empty.
[[nodiscard]] std::string measurementDriveText(double levelDb, LevelConvention convention,
                                               const std::vector<int>& outputs);

}  // namespace rta::measure
