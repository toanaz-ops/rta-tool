// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/CycleArrival.h"

#include "rta/dsp/Biquad.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace rta::measure {
namespace {

using rta::dsp::Biquad;

/// One Butterworth section of an order-`order` prototype. k >= 1 is a conjugate
/// pair with Q = 1 / (2 sin((2k-1) pi / (2 order))); k == 0 is the real pole of
/// an odd order (a first-order section). RBJ/bilinear with the corner prewarped,
/// the same construction SyntheticCrossover.h uses for its Q = 1/sqrt 2 case.
Biquad::Coeffs section(double fcHz, double fs, bool highPass, int order, int k) {
    const double w0 = 2.0 * std::numbers::pi * fcHz / fs;
    if (k == 0) {
        const double t = std::tan(w0 / 2.0);
        const double a0 = 1.0 + t;
        return highPass ? Biquad::Coeffs{ 1.0 / a0, -1.0 / a0, 0.0, (t - 1.0) / a0, 0.0 }
                        : Biquad::Coeffs{ t / a0, t / a0, 0.0, (t - 1.0) / a0, 0.0 };
    }
    const double q = 1.0 / (2.0 * std::sin((2.0 * k - 1.0) * std::numbers::pi / (2.0 * order)));
    const double alpha = std::sin(w0) / (2.0 * q);
    const double cw = std::cos(w0);
    const double a0 = 1.0 + alpha;
    const double b1 = highPass ? 1.0 + cw : 1.0 - cw;
    return { b1 / (2.0 * a0), (highPass ? -b1 : b1) / a0, b1 / (2.0 * a0), -2.0 * cw / a0,
             (1.0 - alpha) / a0 };
}

}  // namespace

std::vector<Biquad::Coeffs> topologyCascade(Topology topology, bool highPass, double crossoverHz, double sampleRate) {
    std::vector<Biquad::Coeffs> sections;
    if (!(crossoverHz > 0.0) || !(sampleRate > 0.0) || crossoverHz >= sampleRate / 2.0) return sections;
    const bool lr = topology.family == CrossoverFamily::LinkwitzRiley;
    const int order = lr ? topology.order / 2 : topology.order;
    if (order < 1 || topology.order > 16 || (lr && topology.order % 2 != 0)) return sections;
    for (int pass = 0; pass < (lr ? 2 : 1); ++pass) {
        if (order % 2 == 1) sections.push_back(section(crossoverHz, sampleRate, highPass, order, 0));
        for (int k = 1; k <= order / 2; ++k) sections.push_back(section(crossoverHz, sampleRate, highPass, order, k));
    }
    return sections;
}

double topologyPeakLagSamples(Topology topology, bool highPass, double crossoverHz, double sampleRate) {
    auto sections = topologyCascade(topology, highPass, crossoverHz, sampleRate);
    if (sections.empty()) return 0.0;
    rta::dsp::BiquadCascade cascade(std::move(sections));

    // Ten corner periods is several decay times of every shipped order; the
    // longest, LR8, has its peak near 0.94 / fc.
    const auto length = static_cast<std::size_t>(std::min(1.0e6, 10.0 * sampleRate / crossoverHz));
    double best = 0.0;
    std::size_t bestIndex = 0;
    for (std::size_t n = 0; n < length; ++n) {
        const double y = std::fabs(cascade.processSample(n == 0 ? 1.0 : 0.0));
        if (y > best) {
            best = y;
            bestIndex = n;
        }
    }
    return static_cast<double>(bestIndex);
}

std::optional<double> candidateWithinGuard(const std::vector<double>& candidatesSeconds, double targetSeconds) {
    if (candidatesSeconds.empty()) return std::nullopt;
    double best = candidatesSeconds.front();
    for (const double c : candidatesSeconds) {
        if (std::fabs(c - targetSeconds) < std::fabs(best - targetSeconds)) best = c;
    }
    if (candidatesSeconds.size() < 2) return best;
    std::vector<double> sorted = candidatesSeconds;
    std::sort(sorted.begin(), sorted.end());
    double spacing = sorted[1] - sorted[0];
    for (std::size_t i = 2; i < sorted.size(); ++i) spacing = std::min(spacing, sorted[i] - sorted[i - 1]);
    if (!(spacing > 0.0)) return best;
    return std::fabs(best - targetSeconds) <= spacing / 4.0 ? std::optional<double>(best) : std::nullopt;
}

}  // namespace rta::measure
