// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/Fft.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace rta::dsp {

namespace {
constexpr double kTwoPi = 2.0 * std::numbers::pi;
}

Fft::Fft(std::size_t size) : size_(size) {
    if (size < 2 || !std::has_single_bit(size)) {
        throw std::invalid_argument("Fft size must be a power of two and at least 2");
    }

    // Bit-reversal permutation. Decimation-in-time visits inputs in this order;
    // applying it up front lets the butterflies run over contiguous memory.
    const int levels = std::countr_zero(size);
    reversed_.resize(size);
    for (std::size_t i = 0; i < size; ++i) {
        std::size_t r = 0;
        for (int bit = 0; bit < levels; ++bit) {
            r |= ((i >> bit) & std::size_t{1}) << (levels - 1 - bit);
        }
        reversed_[i] = r;
    }

    // Twiddles computed in double and stored in float. Generating them by
    // repeated complex multiplication is faster and is what a naive
    // implementation does, but the error compounds along the table and shows up
    // as a rising noise floor in the upper bins -- which in a measurement tool
    // looks exactly like a real acoustic problem.
    twiddles_.resize(size / 2);
    for (std::size_t k = 0; k < twiddles_.size(); ++k) {
        const double angle = -kTwoPi * static_cast<double>(k) / static_cast<double>(size);
        twiddles_[k] = { static_cast<float>(std::cos(angle)),
                         static_cast<float>(std::sin(angle)) };
    }
}

void Fft::transform(std::span<std::complex<float>> data, const bool conjugate) {
    if (data.size() != size_) {
        throw std::invalid_argument("Fft::transform got a span of the wrong length");
    }

    for (std::size_t i = 0; i < size_; ++i) {
        const std::size_t j = reversed_[i];
        if (i < j) std::swap(data[i], data[j]);
    }

    for (std::size_t len = 2; len <= size_; len <<= 1) {
        const std::size_t half = len / 2;
        const std::size_t step = size_ / len;

        for (std::size_t start = 0; start < size_; start += len) {
            for (std::size_t j = 0; j < half; ++j) {
                const auto w = conjugate ? std::conj(twiddles_[j * step])
                                         : twiddles_[j * step];
                const auto upper = data[start + j];
                const auto lower = data[start + j + half] * w;
                data[start + j]        = upper + lower;
                data[start + j + half] = upper - lower;
            }
        }
    }
}

void Fft::forward(std::span<std::complex<float>> data) {
    transform(data, false);
}

void Fft::inverse(std::span<std::complex<float>> data) {
    // The inverse DFT is the forward DFT with conjugated twiddles, scaled by
    // 1/N. No separate code path, so an error cannot exist in one direction
    // only -- which is also why the round-trip test needs the assertion that
    // forward actually moved the data.
    transform(data, true);
    const float scale = 1.0f / static_cast<float>(size_);
    for (auto& value : data) value *= scale;
}

}  // namespace rta::dsp
