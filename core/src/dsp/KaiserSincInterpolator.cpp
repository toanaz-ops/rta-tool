// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/KaiserSincInterpolator.h"

#include <cmath>
#include <stdexcept>

namespace rta::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;

/// I0(y) by its power series sum_k ((y/2)^k / k!)^2. Every term is positive,
/// so there is no cancellation; the recurrence t_k = t_{k-1} (y/2k)^2 stops
/// when a term is below 1e-17 of the sum (far past double's 1.1e-16).
double besselI0(double y) noexcept {
    const double q = 0.25 * y * y;
    double term = 1.0;
    double sum = 1.0;
    for (int k = 1; k < 200; ++k) {
        term *= q / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term < 1e-17 * sum) break;
    }
    return sum;
}

const double kI0Beta = besselI0(KaiserSincInterpolator::kBeta);

}  // namespace

double KaiserSincInterpolator::kernel(double x) noexcept {
    const double ax = std::fabs(x);
    if (ax >= static_cast<double>(kHalfLength)) return 0.0;
    // A normalised sinc is a delta on the integers. sin(pi n) in floating
    // point is ~1e-16, not 0, so evaluating the quotient at an integer would
    // leave a 1e-16 smear that breaks the "integer positions copy the input
    // bit for bit" identity (a 0 * x_i would still be exact, a 1e-16 * x_i is
    // not). Special-case it instead of hoping the window hides it.
    if (x == std::floor(x)) return x == 0.0 ? 1.0 : 0.0;
    const double u = x / static_cast<double>(kHalfLength);
    const double window = besselI0(kBeta * std::sqrt(1.0 - u * u)) / kI0Beta;
    return std::sin(kPi * x) / (kPi * x) * window;
}

KaiserSincInterpolator::InputSpan KaiserSincInterpolator::requiredInputSpan(
    double firstPosition, double step, std::size_t count) {
    if (count == 0 || !std::isfinite(firstPosition) || !std::isfinite(step) || !(step > 0.0)) {
        throw std::invalid_argument(
            "KaiserSincInterpolator: need count > 0, finite firstPosition and step > 0");
    }
    const double lastPosition = firstPosition + static_cast<double>(count - 1) * step;
    // Taps with |position - i| < 32: i in (position - 32, position + 32), i.e.
    // floor(position) - 31 .. ceil(position) + 31 (63 taps at an integer
    // position, 64 otherwise).
    return {static_cast<std::ptrdiff_t>(std::floor(firstPosition)) - (kHalfLength - 1),
            static_cast<std::ptrdiff_t>(std::ceil(lastPosition)) + (kHalfLength - 1)};
}

void KaiserSincInterpolator::resample(std::span<const float> input, double firstPosition,
                                      double step, std::span<float> output) const {
    if (output.empty()) return;
    const InputSpan need = requiredInputSpan(firstPosition, step, output.size());
    if (need.first < 0 || need.last >= static_cast<std::ptrdiff_t>(input.size())) {
        throw std::invalid_argument(
            "KaiserSincInterpolator: a needed tap lies outside the input -- supply a "
            "32-sample margin each side");
    }
    for (std::size_t k = 0; k < output.size(); ++k) {
        const double position = firstPosition + static_cast<double>(k) * step;
        const auto base = static_cast<std::ptrdiff_t>(std::floor(position));
        double acc = 0.0;
        if (static_cast<double>(base) == position) {
            acc = static_cast<double>(input[static_cast<std::size_t>(base)]);  // delta
        } else {
            const auto first = base - (kHalfLength - 1);
            const auto last = base + kHalfLength;
            for (auto i = first; i <= last; ++i) {
                acc += static_cast<double>(input[static_cast<std::size_t>(i)]) *
                       kernel(position - static_cast<double>(i));
            }
        }
        output[k] = static_cast<float>(acc);
    }
}

}  // namespace rta::dsp
