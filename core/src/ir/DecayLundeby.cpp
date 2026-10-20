// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
//
// Lundeby's crossing search, split out of Decay.cpp when that file passed the
// project's 400-line cap. The seam is the one that made it long: everything
// here answers "where does the decay meet the noise floor", and everything left
// behind answers "what is the decay, given that point".
#include "DecayLundeby.h"

#include <algorithm>
#include <cmath>

namespace rta::ir::detail {

double powerToDb(double p) noexcept {
    return 10.0 * std::log10(std::max(p, kTiny));
}

bool blockAverage(std::span<const double> x, std::size_t window,
                                std::vector<double>& levels,
                                std::vector<double>& centres) {
    if (window == 0) window = 1;
    const std::size_t blocks = x.size() / window;
    if (blocks < 2) return false;

    levels.assign(blocks, 0.0);
    centres.assign(blocks, 0.0);
    for (std::size_t b = 0; b < blocks; ++b) {
        double sum = 0.0;
        for (std::size_t i = 0; i < window; ++i) sum += x[b * window + i];
        levels[b] = sum / static_cast<double>(window);
        centres[b] = (static_cast<double>(b) + 0.5) * static_cast<double>(window);
    }
    return true;
}

bool leastSquares(std::span<const double> x, std::span<const double> y,
                                double& slope, double& intercept) {
    const auto n = static_cast<double>(x.size());
    if (x.size() < 2) return false;

    // x is a sample index, up to ~1e6 on a long high-rate record, while its
    // SPREAD may be a few thousand. n*sum(x^2) - (sum x)^2 then subtracts two
    // numbers of order 1e12 to get one of order 1e9 and loses ~3 digits.
    // Centring x on its mean first makes the sums those of the spread alone; the
    // line is shifted back afterwards, so slope and intercept mean what they did.
    double mean = 0.0;
    for (const double v : x) mean += v;
    mean /= n;

    double sy = 0.0, sxx = 0.0, sxy = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double dx = x[i] - mean;
        sy += y[i]; sxx += dx * dx; sxy += dx * y[i];
    }
    if (sxx < 1e-12) return false;

    slope = sxy / sxx;
    intercept = sy / n - slope * mean;
    return true;
}

/// Lundeby's iterative search for where the decay meets the noise floor.
///
/// Derived from the algorithm's description rather than transcribed from an
/// implementation, so that every constant in `DecayConfig` is a decision this
/// project can defend rather than an accident it inherited. The best-known open
/// implementation was consulted afterwards, as a check -- and a public issue
/// against it turned out to describe a state its `main` had already left, which
/// is its own lesson: a bug report has a date.
bool findCrossing(std::span<const double> squared, double sampleRate,
                                const DecayConfig& cfg, Crossing& out) {
    const std::size_t n = squared.size();
    if (n < 32) return false;

    std::vector<double> levels, centres, fitX, fitY;

    // A first window sized in time, used only to obtain a first slope. Every
    // window after this one is sized from the decay itself.
    if (!blockAverage(squared, static_cast<std::size_t>(0.030 * sampleRate),
                      levels, centres)) {
        return false;
    }

    // First noise estimate: the last tenth of the record. This is a starting
    // guess only -- it is replaced below from a point referenced to the decay.
    double noise = 0.0;
    {
        const std::size_t from = static_cast<std::size_t>(0.9 * static_cast<double>(n));
        double sum = 0.0;
        for (std::size_t i = from; i < n; ++i) sum += squared[i];
        const auto count = static_cast<double>(n - from);
        if (count <= 0.0) return false;
        noise = sum / count;
    }
    if (noise <= 0.0) return false;

    bool haveCrossing = false;
    double crossing = 0.0;
    double slope = 0.0;
    double intercept = 0.0;

    for (int iter = 0; iter < cfg.maxIterations; ++iter) {
        const double noiseDb = powerToDb(noise);

        // The fit starts at the PEAK of the smoothed curve, not at its first
        // block.
        //
        // A narrow band does not begin decaying at the arrival: it rings UP
        // first, over roughly its own impulse response. Including that rise in
        // a decay fit flattens the slope and reads long -- measured on a 40 Hz
        // third-octave against a 0.4 s room, a fit from block zero returned
        // 0.963 s where a fit from the peak returns a figure consistent with an
        // independent Python model at 0.607 s. It is a 2.4x error on the
        // quantity the B*T gate reads, in the direction that opens the gate,
        // and precisely in the narrow bands the gate exists to close.
        //
        // "Late decay" is not a loose description; it is what makes the fit a
        // fit of the room rather than of the filter's onset.
        std::size_t peak = 0;
        for (std::size_t i = 1; i < levels.size(); ++i) {
            if (levels[i] > levels[peak]) peak = i;
        }

        // Keep the regression clear of the knee, and end it at the FIRST block
        // that reaches it. Everything at or below `noiseDb + standoff` is where
        // decay and noise are comparable, and a fit that reaches into it
        // flattens -- one-sided, so it reads long.
        //
        // Walking forward from the peak and stopping is Lundeby's own rule, and
        // it is not the same as "the last block above the line": a late event
        // (a door, an HVAC cycle) that rises back above the line after the decay
        // has already met the noise would, under that reading, drag the end of
        // the fit out to itself, across the whole knee between.
        const double usableDb = noiseDb + cfg.standoffDb;
        if (!(powerToDb(levels[peak]) > usableDb)) return false;
        std::size_t lastUsable = peak;
        while (lastUsable + 1 < levels.size() &&
               powerToDb(levels[lastUsable + 1]) > usableDb) {
            ++lastUsable;
        }

        const double top = powerToDb(levels[peak]);
        fitX.clear();
        fitY.clear();
        for (std::size_t i = peak; i <= lastUsable; ++i) {
            const double db = powerToDb(levels[i]);
            if (db <= top && db >= top - cfg.fitRangeDb) {
                fitX.push_back(centres[i]);
                fitY.push_back(db);
            }
        }
        if (fitX.size() < 2) return false;

        if (!leastSquares(fitX, fitY, slope, intercept)) return false;
        if (slope >= 0.0) return false;

        const double next = (noiseDb - intercept) / slope;
        const bool converged = haveCrossing &&
            std::abs(next - crossing) / sampleRate < cfg.convergenceSec;
        crossing = next;
        haveCrossing = true;
        if (converged) break;

        // One place derives the decay rate; both the window and the noise
        // margin below are expressed through it.
        const double dbPerSample = -slope;
        if (!(dbPerSample > 0.0)) return false;

        const double window = (10.0 / dbPerSample) / cfg.intervalsPer10Db;
        if (!(window >= 1.0) || !std::isfinite(window)) return false;
        if (!blockAverage(squared, static_cast<std::size_t>(window), levels, centres)) {
            return false;
        }

        // Re-estimate the noise from beyond the crossing, offset by a fixed
        // amount of DECAY rather than a fraction of the record. A record
        // fraction would tie the estimate to how long the operator recorded
        // for -- the dependency truncation exists to remove.
        const double margin = cfg.noiseMarginDb / dbPerSample;
        const double startF = crossing + margin;
        if (!(startF >= 0.0) || !std::isfinite(startF)) return false;
        if (startF >= static_cast<double>(n)) return false;

        const auto start = static_cast<std::size_t>(startF);
        if (n - start < 16) return false;   // refuse; never fall back to 0.9*n

        double sum = 0.0;
        for (std::size_t i = start; i < n; ++i) sum += squared[i];
        noise = sum / static_cast<double>(n - start);
        if (!(noise > 0.0)) return false;
    }

    if (!haveCrossing || !(crossing > 0.0) || crossing >= static_cast<double>(n)) {
        return false;
    }

    out.index = static_cast<std::size_t>(crossing);
    out.noisePower = noise;
    out.slopeDbPerSample = -slope;
    out.interceptDb = intercept;
    return out.index >= 16;
}

}  // namespace rta::ir::detail
