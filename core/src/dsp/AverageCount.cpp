// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/AverageCount.h"

#include <cmath>

namespace rta::dsp {

double overlapCorrelation(std::span<const float> window, std::size_t lag) noexcept {
    // Lag 0 is the definition's own normalisation: a frame correlates
    // perfectly with itself.
    if (lag == 0) {
        return 1.0;
    }
    // No sample position is shared once the lag reaches the block length --
    // there is nothing left to sum.
    if (lag >= window.size()) {
        return 0.0;
    }

    double sumSquares = 0.0;
    for (const float w : window) {
        sumSquares += static_cast<double>(w) * static_cast<double>(w);
    }
    // A window of all zeros correlates with nothing; report that rather than
    // dividing by zero.
    if (sumSquares <= 0.0) {
        return 0.0;
    }

    // The numerator runs ONLY over samples where both the frame and its
    // lag-shifted copy exist -- it must NOT wrap around the block. Wrapping
    // turns periodic Hann's exact 1/6 at 50 % overlap into 1/3 (see the header
    // comment), which is a wrong number that still looks plausible.
    double numerator = 0.0;
    for (std::size_t n = 0; n + lag < window.size(); ++n) {
        numerator += static_cast<double>(window[n]) * static_cast<double>(window[n + lag]);
    }
    return numerator / sumSquares;
}

double fifoEffectiveAverages(std::span<const float> window, std::size_t hop,
                             std::size_t frames) noexcept {
    if (frames == 0) {
        return 0.0;
    }
    // hop == 0 means every frame is the SAME samples again: c(0) == 1.0 at
    // every lag the loop below would try, so it would never find one that is
    // zero. However many frames there are, they carry exactly one
    // independent average's worth of information.
    if (hop == 0) {
        return 1.0;
    }
    const double k = static_cast<double>(frames);

    // Harris 1978: overlapped frames are correlated, so each one beyond the
    // first buys less than a full independent average. c(m*hop) is exactly
    // zero once m*hop reaches the window length, so the loop terminates on
    // the window length -- the cost of this function does not grow with K.
    double sum = 0.0;
    for (std::size_t m = 1; m * hop < window.size() && m < frames; ++m) {
        const double c = overlapCorrelation(window, m * hop);
        sum += (1.0 - static_cast<double>(m) / k) * c * c;
    }
    return k / (1.0 + 2.0 * sum);
}

double exponentialEffectiveAverages(std::span<const float> window, std::size_t hop,
                                    double alpha, std::size_t frames) noexcept {
    if (frames == 0) {
        return 0.0;
    }
    // hop == 0 carries the same precondition as fifoEffectiveAverages, for
    // the same reason: every frame is the same samples again, so the overlap
    // sum below would never find a lag at which c reaches zero. Guard it
    // before that loop, not after -- the loop is what would hang.
    if (hop == 0) {
        return 1.0;
    }
    // Alpha outside (0, 1] is not a valid one-pole coefficient; clamp rather
    // than let a caller's mistake propagate into a NaN or negative average
    // count. Written in positive form (rather than `alpha <= 0.0` /
    // `alpha > 1.0`) so a NaN alpha -- for which BOTH of those comparisons
    // are false -- still takes a clamped branch instead of slipping through
    // and poisoning every average count downstream.
    if (!(alpha > 0.0)) {
        alpha = 1e-12;
    } else if (!(alpha <= 1.0)) {
        alpha = 1.0;
    }

    const double k = static_cast<double>(frames);
    // Weights after K frames are w1 = (1-a)^(K-1), wi = a(1-a)^(K-i) for
    // i = 2..K; they sum to 1. Neff_raw = 1 / sum(wi^2) is the standard
    // "effective sample size" of a weighted mean (Kish's formula), evaluated
    // in closed form for this particular geometric weight sequence:
    //   sum(wi^2) = (1-a)^(2(K-1)) * (2-2a)/(2-a) + a/(2-a)
    const double onePower = std::pow(1.0 - alpha, 2.0 * (k - 1.0));
    const double s = onePower * (2.0 - 2.0 * alpha) / (2.0 - alpha) + alpha / (2.0 - alpha);
    const double raw = 1.0 / s;

    // The overlap penalty from Harris 1978 applies to the frames BEYOND the
    // first, exactly as it does for the FIFO case, except the exponential
    // average has no finite horizon: the sum runs over every lag the window
    // still overlaps at, not just the ones seen within `frames`.
    double overlapSum = 0.0;
    for (std::size_t m = 1; m * hop < window.size(); ++m) {
        const double c = overlapCorrelation(window, m * hop);
        overlapSum += c * c;
    }
    const double d = 1.0 + 2.0 * overlapSum;

    return 1.0 + (raw - 1.0) / d;
}

}  // namespace rta::dsp
