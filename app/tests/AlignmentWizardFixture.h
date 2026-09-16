// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Shared fixtures for L7-ALIGN task H, split the way EqSessionFixture.h is:
// test_alignment_wizard.cpp drives the L7-OUT sequence and the refusals,
// test_alignment_wizard_verdict.cpp drives the fit, the polarity table and the
// structural scans. Both need the same synthetic pair, and a second copy of it
// would be a second thing to keep correct.
#pragma once

#include "CodeLines.h"
#include "measure/AlignmentWizard.h"
#include "trace/Trace.h"

#include "rta/gen/Noise.h"
#include "rta/platform/OutputEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace rta::test {

inline constexpr double kPi = std::numbers::pi;
inline constexpr double kFs = 48000.0;
inline constexpr int kFftSize = 512;
inline constexpr int kChannels = 4;

[[nodiscard]] inline std::size_t pointCount() { return rta::trace::pointCountFor(kFftSize); }
[[nodiscard]] inline double binWidthHz() { return kFs / static_cast<double>(kFftSize); }

/// Runs the engine's callback for `blocks` blocks of `blockSize`. No device
/// anywhere in this path -- the same shape test_eq_verify.cpp uses.
inline void pump(rta::platform::OutputEngine& engine, int blocks, int blockSize = 512) {
    for (int b = 0; b < blocks; ++b) {
        std::vector<std::vector<float>> buffers(static_cast<std::size_t>(kChannels),
                                                std::vector<float>(
                                                    static_cast<std::size_t>(blockSize), 0.0f));
        std::vector<float*> pointers;
        pointers.reserve(buffers.size());
        for (auto& channel : buffers) pointers.push_back(channel.data());
        engine.render(pointers.data(), kChannels, blockSize);
    }
}

[[nodiscard]] inline int routedCount(const rta::platform::OutputEngine& engine) {
    int routed = 0;
    for (int ch = 0; ch < rta::platform::kMaxChannels; ++ch) {
        if (engine.role(ch) == rta::platform::OutputRole::Routed) ++routed;
    }
    return routed;
}

/// A capture with a caller-chosen magnitude and phase, full coherence, and a
/// meta a pair check can be made to disagree on one field at a time.
[[nodiscard]] inline rta::trace::Trace
makeCapture(const std::string& id, const std::vector<float>& magnitudeDb,
            const std::vector<float>& phase, int appliedDelaySamples = 0,
            double sampleRate = kFs, int fftSize = kFftSize,
            const std::string& channelRoles = "ref:1 meas:2") {
    rta::trace::CaptureMeta meta;
    meta.id = id;
    meta.sampleRate = sampleRate;
    meta.fftSize = fftSize;
    meta.channelRoles = channelRoles;
    meta.appliedDelaySamples = appliedDelaySamples;

    auto trace = rta::trace::Trace::make(meta, magnitudeDb);
    REQUIRE(trace.has_value());
    REQUIRE(trace->setPhase(phase));
    REQUIRE(trace->setCoherence(std::vector<float>(magnitudeDb.size(), 1.0f)));
    return std::move(*trace);
}

/// A capture with a magnitude and NO phase -- what a single-channel capture
/// really is (Trace.h's class comment: nothing ever has to invent one). It has
/// no complex form, so the wizard's fit cannot run on it.
[[nodiscard]] inline rta::trace::Trace makeCaptureWithoutPhase(const std::string& id,
                                                               const std::vector<float>& magnitudeDb) {
    rta::trace::CaptureMeta meta;
    meta.id = id;
    meta.sampleRate = kFs;
    meta.fftSize = kFftSize;
    meta.channelRoles = "ref:1 meas:2";

    auto trace = rta::trace::Trace::make(meta, magnitudeDb);
    REQUIRE(trace.has_value());
    REQUIRE_FALSE(trace->has(rta::trace::Field::Phase));
    return std::move(*trace);
}

/// Rising (high-pass-ish) and falling (low-pass-ish) magnitudes that CROSS at
/// the middle bin, so spectralCrossover has a real crossing to find rather
/// than two flat curves that never meet.
[[nodiscard]] inline std::vector<float> risingDb() {
    std::vector<float> db(pointCount());
    for (std::size_t k = 0; k < db.size(); ++k) {
        db[k] = static_cast<float>(-20.0 + 40.0 * static_cast<double>(k)
                                              / static_cast<double>(db.size() - 1));
    }
    return db;
}

[[nodiscard]] inline std::vector<float> fallingDb() {
    std::vector<float> db(pointCount());
    for (std::size_t k = 0; k < db.size(); ++k) {
        db[k] = static_cast<float>(+20.0 - 40.0 * static_cast<double>(k)
                                              / static_cast<double>(db.size() - 1));
    }
    return db;
}

[[nodiscard]] inline std::vector<float> constantPhase(double radians) {
    return std::vector<float>(pointCount(), static_cast<float>(radians));
}

/// arg = wrap(-2*pi*f*T): a pure delay of T seconds on this side, which is
/// what makes BandFit::tauSeconds a signed quantity a transposition can flip.
[[nodiscard]] inline std::vector<float> delayedPhase(double delaySeconds) {
    std::vector<float> phase(pointCount());
    for (std::size_t k = 0; k < phase.size(); ++k) {
        const double hz = static_cast<double>(k) * binWidthHz();
        double radians = std::remainder(-2.0 * kPi * hz * delaySeconds, 2.0 * kPi);
        if (radians <= -kPi) radians += 2.0 * kPi;
        phase[k] = static_cast<float>(radians);
    }
    return phase;
}

/// Fit options with a SMALL bounded search: the shipped defaults (20 ms at a
/// 1 us grid) are 40001 grid points per fit, which is right for a rack and
/// wrong for a test that runs a dozen fits. The record refuses baked-in
/// defaults anyway (Sec.4, Sec.12), so every caller states its own.
[[nodiscard]] inline rta::measure::AlignmentWizard::Config makeConfig() {
    rta::measure::AlignmentWizard::Config config;
    config.mainOutputChannel = 0;
    config.subOutputChannel = 1;
    config.sampleRate = kFs;
    config.minimumGatedCoherence = 0.5;
    config.fit.octavesEachSide = 1.0;
    config.fit.tauRangeSeconds = 1.0e-3;
    config.fit.tauGridSeconds = 1.0e-5;
    config.fit.maxCycleCandidates = 3;
    return config;
}

/// The wizard's excitation, supplied by the CALLER the way DelayLocator's and
/// EqVerify's are -- so a pass is reproducible bit-for-bit against the same
/// room, and so nothing constructs a source behind the operator's back.
[[nodiscard]] inline rta::platform::SourceVariant pinkNoise() {
    return rta::gen::PinkNoise(rta::gen::Pcg32(1u, 1u), -12.0);
}

/// Drives the two captures through the wizard in the order its own answered
/// high-pass side dictates: the FIRST capture is always the high-pass side's,
/// because that is the output the sequence solos first.
inline void runSequence(rta::measure::AlignmentWizard& wizard,
                        rta::platform::OutputEngine& engine, rta::trace::Trace first,
                        rta::trace::Trace second) {
    wizard.arm();
    REQUIRE(wizard.lastRefusal() == rta::measure::WizardRefusal::None);
    pump(engine, 2);
    wizard.poll();
    wizard.submitCapture(std::move(first));
    pump(engine, 2);
    wizard.poll();
    wizard.submitCapture(std::move(second));
}

}  // namespace rta::test
