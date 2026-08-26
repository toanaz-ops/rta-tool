// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/Window.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace rta::dsp {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

/// Generalised cosine window, periodic form:
///   w[n] = sum_k (-1)^k * a[k] * cos(2*pi*k*n / N)
double cosineSum(std::span<const double> a, std::size_t n, std::size_t N) {
    const double x = kTwoPi * static_cast<double>(n) / static_cast<double>(N);
    double w = 0.0;
    double sign = 1.0;
    for (std::size_t k = 0; k < a.size(); ++k) {
        w += sign * a[k] * std::cos(static_cast<double>(k) * x);
        sign = -sign;
    }
    return w;
}

double tukey(std::size_t n, std::size_t N, double alpha) {
    if (alpha <= 0.0) return 1.0;
    if (alpha > 1.0) alpha = 1.0;
    const double x = static_cast<double>(n) / static_cast<double>(N);
    const double half = alpha / 2.0;
    if (x < half) {
        return 0.5 * (1.0 + std::cos(kTwoPi / alpha * (x - half)));
    }
    if (x <= 1.0 - half) {
        return 1.0;
    }
    return 0.5 * (1.0 + std::cos(kTwoPi / alpha * (x - 1.0 + half)));
}

}  // namespace

std::string_view toString(WindowType type) noexcept {
    switch (type) {
        case WindowType::Rectangular:    return "Rectangular";
        case WindowType::Hann:           return "Hann";
        case WindowType::Hamming:        return "Hamming";
        case WindowType::BlackmanHarris: return "Blackman-Harris";
        case WindowType::FlatTop:        return "Flat-top";
        case WindowType::Tukey:          return "Tukey";
    }
    return "Unknown";
}

Window::Window(WindowType type, std::size_t size, double param) : type_(type) {
    if (size == 0) {
        throw std::invalid_argument("Window size must be greater than zero");
    }
    coefficients_.resize(size);

    // Coefficient sets, all periodic (denominator N).
    static constexpr double kHann[]           = {0.5, 0.5};
    static constexpr double kHamming[]        = {0.54, 0.46};
    static constexpr double kBlackmanHarris[] = {0.35875, 0.48829, 0.14128, 0.01168};
    static constexpr double kFlatTop[]        = {0.21557895, 0.41663158, 0.277263158,
                                                 0.083578947, 0.006947368};

    for (std::size_t n = 0; n < size; ++n) {
        double w = 1.0;
        switch (type) {
            case WindowType::Rectangular:    w = 1.0; break;
            case WindowType::Hann:           w = cosineSum(kHann, n, size); break;
            case WindowType::Hamming:        w = cosineSum(kHamming, n, size); break;
            case WindowType::BlackmanHarris: w = cosineSum(kBlackmanHarris, n, size); break;
            case WindowType::FlatTop:        w = cosineSum(kFlatTop, n, size); break;
            case WindowType::Tukey:          w = tukey(n, size, param); break;
        }
        coefficients_[n] = static_cast<float>(w);
        sum_ += w;
        sumSquares_ += w * w;
    }
}

double Window::coherentGain() const noexcept {
    return sum_ / static_cast<double>(size());
}

double Window::amplitudeCorrection() const noexcept {
    return static_cast<double>(size()) / sum_;
}

double Window::energyCorrection() const noexcept {
    return std::sqrt(static_cast<double>(size()) / sumSquares_);
}

double Window::equivalentNoiseBandwidth() const noexcept {
    return static_cast<double>(size()) * sumSquares_ / (sum_ * sum_);
}

void Window::apply(std::span<const float> in, std::span<float> out) const {
    assert(in.size() == size() && out.size() == size());
    const std::size_t n = std::min({in.size(), out.size(), coefficients_.size()});
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = in[i] * coefficients_[i];
    }
}

}  // namespace rta::dsp
