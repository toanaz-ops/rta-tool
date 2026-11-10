// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/ClockDrift.h"

#include "rta/dsp/DualFftEngine.h"
#include "rta/dsp/TransferEstimator.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <stdexcept>
#include <vector>

namespace rta::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kFftSize = 4096;
constexpr std::size_t kHopSize = 1024;
// Weight = gamma^2 / (1 - gamma^2) diverges at gamma^2 = 1 (a noiseless pair).
// Clamping 1 - gamma^2 at 1e-6 keeps it finite and lets a perfect bin carry
// 1e6 times a gamma^2 = 0.5 bin instead of infinity.
constexpr double kOneMinusGammaFloor = 1e-6;
constexpr double kMaxResidualSamples = 1.0;

double median(std::vector<double> v) {
    const std::size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid), v.end());
    const double upper = v[mid];
    if (v.size() % 2 == 1) return upper;
    const double lower = *std::max_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid));
    return 0.5 * (lower + upper);
}

}  // namespace

std::optional<FractionalDelay> fractionalDelayByPhaseSlope(std::span<const float> reference,
                                                           std::span<const float> measurement,
                                                           std::ptrdiff_t integerDelay,
                                                           const PhaseSlopeOptions& options) {
    if (reference.size() != measurement.size()) {
        throw std::invalid_argument("fractionalDelayByPhaseSlope: spans must be the same length");
    }
    if (!(options.sampleRate > 0.0)) {
        throw std::invalid_argument("fractionalDelayByPhaseSlope: sampleRate must be positive");
    }
    const double maxHz = options.maxHz > 0.0 ? options.maxHz : options.sampleRate / 4.0;
    if (maxHz < options.minHz) {
        throw std::invalid_argument("fractionalDelayByPhaseSlope: maxHz below minHz");
    }

    DualFftEngine::Config config;
    config.fftSize = kFftSize;
    config.hopSize = kHopSize;
    config.sampleRate = options.sampleRate;
    config.window = WindowType::Hann;
    config.averaging = TransferAveraging::Cumulative;
    // Range-check before narrowing; the engine itself rejects |delay| > 1<<20.
    if (integerDelay < std::numeric_limits<int>::min() ||
        integerDelay > std::numeric_limits<int>::max()) {
        throw std::invalid_argument("fractionalDelayByPhaseSlope: integerDelay out of range");
    }
    config.referenceDelaySamples = static_cast<int>(integerDelay);
    DualFftEngine engine(config);
    engine.process(reference, measurement);

    // The ONLY coherence source (check_coherence_gate.cmake): the snapshot
    // withholds it below 8 effective averages, and then there is no weight to
    // fit with.
    const TransferSnapshot snapshot = makeSnapshot(engine, Estimator::H1);
    if (!snapshot.coherence) return std::nullopt;

    struct Point {
        double f, phase, weight;
    };
    std::vector<Point> points;
    double previous = 0.0;
    double offset = 0.0;  // accumulated multiple of 2 pi
    // Bins 0 and N/2 are real for a real signal: their H1 phase is 0 or pi
    // whatever the delay, so they carry no delay information and, at the very
    // end of the frequency axis, would carry the most leverage on the slope.
    for (std::size_t k = 1; k + 1 < snapshot.h.size(); ++k) {
        const double f = engine.binFrequency(k);
        if (f < options.minHz || f > maxHz) continue;
        const double gamma2 = static_cast<double>((*snapshot.coherence)[k]);
        if (!(gamma2 >= options.minCoherence)) continue;
        // Unwrap along frequency over the SELECTED bins only: a bin the
        // coherence gate dropped is noise and must not steer the branch.
        double phase = std::arg(snapshot.h[k]) + offset;
        if (!points.empty()) {
            while (phase - previous > kPi) { phase -= 2.0 * kPi; offset -= 2.0 * kPi; }
            while (phase - previous < -kPi) { phase += 2.0 * kPi; offset += 2.0 * kPi; }
        }
        previous = phase;
        const double oneMinus = std::max(1.0 - gamma2, kOneMinusGammaFloor);
        points.push_back({f, phase, gamma2 / oneMinus});
    }
    if (points.size() < options.minBins || points.size() < 2) return std::nullopt;

    // Weighted least squares phi = a f + c, centred on the weighted means so
    // the normal equations stay well conditioned (f spans ~1e4 Hz, a ~1e-4).
    double sw = 0.0, sf = 0.0, sp = 0.0;
    for (const auto& p : points) { sw += p.weight; sf += p.weight * p.f; sp += p.weight * p.phase; }
    const double fMean = sf / sw, pMean = sp / sw;
    double sxx = 0.0, sxy = 0.0;
    for (const auto& p : points) {
        sxx += p.weight * (p.f - fMean) * (p.f - fMean);
        sxy += p.weight * (p.f - fMean) * (p.phase - pMean);
    }
    if (!(sxx > 0.0)) return std::nullopt;
    const double slope = sxy / sxx;  // rad / Hz
    const double intercept = pMean - slope * fMean;

    // Engine convention: H1 = Sxy / Sxx with Sxy = conj(X) Y, so a measurement
    // that lags by tau seconds has phase -2 pi f tau; tau = -slope / (2 pi).
    const double residualSamples = -slope / (2.0 * kPi) * options.sampleRate;
    if (std::fabs(residualSamples) > kMaxResidualSamples) return std::nullopt;

    return FractionalDelay{static_cast<double>(integerDelay) + residualSamples, intercept,
                           points.size()};
}

std::optional<DriftFit> fitDriftTheilSen(std::span<const DelayObservation> points) {
    if (points.size() < 2) return std::nullopt;
    std::vector<double> slopes;
    slopes.reserve(points.size() * (points.size() - 1) / 2);
    for (std::size_t i = 0; i < points.size(); ++i) {
        for (std::size_t j = i + 1; j < points.size(); ++j) {
            const double dp = points[j].positionSamples - points[i].positionSamples;
            if (dp == 0.0) continue;
            slopes.push_back((points[j].delaySamples - points[i].delaySamples) / dp);
        }
    }
    if (slopes.empty()) return std::nullopt;
    const double slope = median(std::move(slopes));

    std::vector<double> intercepts;
    intercepts.reserve(points.size());
    for (const auto& p : points) intercepts.push_back(p.delaySamples - slope * p.positionSamples);
    const double intercept = median(std::move(intercepts));

    double sumSq = 0.0, maxAbs = 0.0;
    for (const auto& p : points) {
        const double r = p.delaySamples - (intercept + slope * p.positionSamples);
        sumSq += r * r;
        maxAbs = std::max(maxAbs, std::fabs(r));
    }
    return DriftFit{intercept, slope, std::sqrt(sumSq / static_cast<double>(points.size())),
                    maxAbs, points.size()};
}

}  // namespace rta::dsp
