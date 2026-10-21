// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
#include "measure/GeneratorModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rta::measure {

namespace {

std::string oneDecimal(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", v);
    return buf;
}

}  // namespace

double clampGeneratorLevelDb(double db) noexcept {
    if (std::isnan(db)) return kGeneratorFloorDb;
    return std::clamp(db, kGeneratorFloorDb, kGeneratorCeilingDb);
}

double stepGeneratorLevelDb(double current, int steps) noexcept {
    return clampGeneratorLevelDb(current + static_cast<double>(steps) * kGeneratorStepDb);
}

rta::platform::SourceVariant makeSource(const GeneratorSettings& settings, double sampleRate,
                                         std::uint64_t seed) {
    // The ceiling lives here. Every caller passes through this line.
    const double level = clampGeneratorLevelDb(settings.levelDb);
    switch (settings.signal) {
        case GeneratorSignal::White:
            return rta::gen::WhiteNoise(rta::gen::Pcg32(seed, 1), level);
        case GeneratorSignal::Sine: {
            // Keep the tone below Nyquist whatever was asked for.
            const double hz = std::clamp(settings.frequencyHz, 1.0, 0.45 * sampleRate);
            return rta::gen::Oscillator(sampleRate, hz, level);
        }
        case GeneratorSignal::Pink:
            break;
    }
    return rta::gen::PinkNoise(rta::gen::Pcg32(seed, 1), level);
}

std::vector<int> pruneOutputs(std::vector<int> outputs, int numOutputs) {
    outputs.erase(std::remove_if(outputs.begin(), outputs.end(),
                                 [&](int ch) { return ch < 0 || (numOutputs > 0 && ch >= numOutputs); }),
                  outputs.end());
    std::sort(outputs.begin(), outputs.end());
    outputs.erase(std::unique(outputs.begin(), outputs.end()), outputs.end());
    if (outputs.empty()) outputs.push_back(0);
    return outputs;
}

std::string signalName(GeneratorSignal signal) {
    switch (signal) {
        case GeneratorSignal::White: return "white";
        case GeneratorSignal::Sine: return "sine";
        case GeneratorSignal::Pink: break;
    }
    return "pink";
}

std::string levelText(const GeneratorSettings& settings) {
    // Noise is calibrated by RMS, a sine by its peak (the engine's own
    // conventions, gen/Noise.h and gen/Oscillator.h): say which.
    const char* unit = settings.signal == GeneratorSignal::Sine ? "dBFS peak" : "dBFS RMS";
    return oneDecimal(settings.levelDb) + " " + unit;
}

std::string frequencyText(double hz) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f Hz", hz);
    return buf;
}

std::string outputsText(const std::vector<int>& outputs) {
    std::string text = "out ";
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        if (i > 0) text += '+';
        text += std::to_string(outputs[i] + 1);
    }
    return text;
}

}  // namespace rta::measure
