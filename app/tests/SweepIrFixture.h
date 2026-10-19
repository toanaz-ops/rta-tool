// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Shared fixtures for the SWEEP lane's pipeline tests (T3/T5): the capture the
// plan's tolerance section describes -- [P zeros, sweep, tail zeros] on the
// reference channel, and the same signal delayed or filtered on the
// measurement channel. JUCE-free.
#pragma once

#include "measure/SweepIrOutcome.h"
#include "rta/gen/Sweep.h"

#include <cstddef>
#include <vector>

namespace sweep_fixture {

/// The sweep exactly as `Sweep::process` renders it, `lengthSamples()` long.
inline std::vector<float> sweepSignal(const rta::gen::Sweep::Config& config) {
    rta::gen::Sweep sweep(config);
    std::vector<float> out(sweep.lengthSamples());
    sweep.process(out);
    return out;
}

/// [preRoll zeros, `signal`, zeros] padded to `totalLength`.
inline std::vector<float> placed(const std::vector<float>& signal, std::size_t preRoll,
                                 std::size_t totalLength) {
    std::vector<float> out(totalLength, 0.0f);
    for (std::size_t i = 0; i < signal.size() && preRoll + i < totalLength; ++i) {
        out[preRoll + i] = signal[i];
    }
    return out;
}

/// `x` shifted later by `delay` samples, same length (the end falls off).
inline std::vector<float> delayed(const std::vector<float>& x, std::size_t delay) {
    std::vector<float> out(x.size(), 0.0f);
    for (std::size_t i = delay; i < x.size(); ++i) out[i] = x[i - delay];
    return out;
}

/// A loop-and-delay capture: reference = [P zeros, sweep, zeros], measurement =
/// the reference delayed by `delay`, both `preRoll + sweep + tail` long.
inline rta::measure::SweepCapture delayCapture(const rta::gen::Sweep::Config& config,
                                               std::size_t preRoll, std::size_t tailSamples,
                                               std::size_t delay) {
    const auto signal = sweepSignal(config);
    const std::size_t total = preRoll + signal.size() + tailSamples;
    rta::measure::SweepCapture capture;
    capture.reference = placed(signal, preRoll, total);
    capture.measurement = delayed(capture.reference, delay);
    return capture;
}

}  // namespace sweep_fixture
