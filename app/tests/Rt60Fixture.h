// SPDX-License-Identifier: AGPL-3.0-or-later
//
// RT60-UI lane (docs/plans/2026-10-20-rt60-ui-lane-plan.md), "Option B": a
// SweepIr built from CLOSED-FORM parts, so a test controls the sweep's trusted
// band, the tail, the artefact region and the decay of each octave band to the
// last digit. JUCE-free.
//
// A band's decay is band-limited white noise multiplied by exp(-t/tau) with
// tau = T / (3 ln 10) in AMPLITUDE (power falls 60 dB in T seconds). The
// Schroeder integral of that is a straight line of slope exactly -60/T in
// expectation (Schroeder 1965) -- the truth every bound in the tests is
// derived from. One noise realisation scatters around it; the tests' bounds are
// the half-gap to the named mutant's answer, never tighter, and each test
// prints the figure beside its bound.
#pragma once

#include "measure/SweepIrOutcome.h"
#include "rta/ir/Decay.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rt60_fixture {

/// xorshift32, bit-identical on every platform (the generator SyntheticInput
/// uses), turned into roughly Gaussian samples by summing twelve uniforms.
class Noise {
public:
    explicit Noise(std::uint32_t seed) : state_(seed != 0 ? seed : 1u) {}

    float gaussian() {
        float sum = 0.0f;
        for (int i = 0; i < 12; ++i) sum += uniform();
        return sum - 6.0f;
    }

private:
    float uniform() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<float>(state_ >> 8) / 16777216.0f;
    }
    std::uint32_t state_;
};

/// One octave-band-limited decay inside the IR.
struct Component {
    double lowHz = 0.0;
    double highHz = 0.0;
    double t60 = 1.0;       ///< seconds for the POWER to fall 60 dB
    std::uint32_t seed = 1;
};

struct IrSpec {
    double fs = 48000.0;
    std::size_t originIndex = 20000;  ///< t = 0, the loop's peak
    long arrivalSamples = 0;          ///< where the decay starts, after t = 0
    std::size_t tailSamples = 144000; ///< what the capture held after the sweep
    /// Samples BEYOND origin + tail: the artefact region a sweep IR carries.
    std::size_t artefactSamples = 48000;
    /// The artefact's level above the decay's starting level, dB; 40 is the
    /// plan's "+40 dB decaying ramp". Negative infinity-ish (-200) leaves it empty.
    double artefactDb = -200.0;
    /// White floor, dB re the decay's unit band rms (-45 gives >= 58 dB of
    /// in-band signal-to-noise in every octave from 125 Hz up).
    double floorDb = -45.0;
    double harmonicSpacingL = 0.0;
    double trustedLowHz = 80.0;
    double trustedHighHz = 19700.0;
    std::vector<Component> components;
    std::uint32_t floorSeed = 99;
};

inline std::vector<float> bandLimitedNoise(const Component& c, std::size_t n, double fs) {
    Noise noise(c.seed);
    std::vector<float> white(n);
    for (auto& v : white) v = noise.gaussian();
    auto band = rta::ir::bandFilterZeroPhase(white, c.lowHz, c.highHz, fs);
    double sumSquares = 0.0;
    for (const float v : band) sumSquares += static_cast<double>(v) * v;
    const double rms = std::sqrt(sumSquares / static_cast<double>(n));
    for (auto& v : band) v = static_cast<float>(v / rms);  // unit rms in band
    return band;
}

inline rta::measure::SweepIr buildIr(const IrSpec& spec) {
    rta::measure::SweepIr ir;
    const std::size_t total = spec.originIndex + spec.tailSamples + spec.artefactSamples;
    ir.samples.assign(total, 0.0f);
    const std::size_t arrival = static_cast<std::size_t>(static_cast<long>(spec.originIndex) + spec.arrivalSamples);

    for (const auto& c : spec.components) {
        const auto noise = bandLimitedNoise(c, total - arrival, spec.fs);
        // Amplitude e-folding time: power -60 dB at T means amplitude -60 dB =
        // 1e-3 at T, so tau = T / ln(1000).
        const double tau = c.t60 / std::log(1000.0);
        for (std::size_t i = 0; i < noise.size(); ++i) {
            const double t = static_cast<double>(i) / spec.fs;
            ir.samples[arrival + i] += static_cast<float>(noise[i] * std::exp(-t / tau));
        }
    }

    // The artefact past the tail: a slowly decaying ramp `artefactDb` above the
    // decay's start. Its apparent RT60 is the sweep's, not the room's (record
    // L4b section 8).
    if (spec.artefactDb > -150.0) {
        Noise noise(spec.floorSeed + 7);
        const double amplitude = std::pow(10.0, spec.artefactDb / 20.0);
        for (std::size_t i = 0; i < spec.artefactSamples; ++i) {
            const double t = static_cast<double>(i) / spec.fs;
            ir.samples[spec.originIndex + spec.tailSamples + i] +=
                static_cast<float>(amplitude * std::exp(-t / 2.0) * noise.gaussian());
        }
    }

    // A white floor, applied to everything BEFORE the artefact (the crop keeps
    // the artefact out; the floor is what Lundeby needs to find a crossing).
    Noise floorNoise(spec.floorSeed);
    const double floorRms = std::pow(10.0, spec.floorDb / 20.0);
    for (std::size_t i = 0; i < spec.originIndex + spec.tailSamples; ++i) {
        ir.samples[i] += static_cast<float>(floorRms * floorNoise.gaussian());
    }

    ir.originIndex = spec.originIndex;
    ir.arrivalSamples = spec.arrivalSamples;
    ir.tailSamples = spec.tailSamples;
    ir.sampleRate = spec.fs;
    ir.harmonicSpacingL = spec.harmonicSpacingL;
    ir.trustedLowHz = spec.trustedLowHz;
    ir.trustedHighHz = spec.trustedHighHz;
    return ir;
}

/// Every octave from 63 Hz to 8 kHz decaying with the same T60: the table a
/// perfect room would give. Edges are the base-ten octave edges (centre / and *
/// 10^(3/20)).
inline std::vector<Component> flatRoom(double t60, std::uint32_t seedBase = 1000) {
    std::vector<Component> out;
    std::uint32_t seed = seedBase;
    for (const double centre : {63.0957, 125.893, 251.189, 501.187, 1000.0, 1995.26, 3981.07, 7943.28}) {
        const double edge = std::pow(10.0, 3.0 / 20.0);
        out.push_back({centre / edge, centre * edge, t60, seed++});
    }
    return out;
}

}  // namespace rt60_fixture
