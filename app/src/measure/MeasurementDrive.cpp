// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
#include "measure/MeasurementDrive.h"

#include <cstdio>

namespace rta::measure {

MeasurementDrive measurementDrive(MeasurementLevelMode mode, const GeneratorSettings& generator,
                                  int numOutputs) {
    MeasurementDrive drive;
    if (mode == MeasurementLevelMode::Own) return drive;  // -12 dBFS, own outputs
    drive.levelDb = clampGeneratorLevelDb(generator.levelDb);
    drive.outputs = pruneOutputs(generator.outputs, numOutputs);
    return drive;
}

std::string measurementLevelText(double levelDb, LevelConvention convention) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.1f dBFS %s", levelDb, convention == LevelConvention::Peak ? "peak" : "RMS");
    return buf;
}

std::string measurementDriveText(double levelDb, LevelConvention convention, const std::vector<int>& outputs) {
    std::string text = measurementLevelText(levelDb, convention);
    if (!outputs.empty()) text += ", " + outputsText(outputs);
    return text;
}

}  // namespace rta::measure
